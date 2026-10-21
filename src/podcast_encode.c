#include "podcast.h"
#if PODCAST_SUPPORT
#include "podcast_encode.h"
#include "podcast_trim.h"
#include "toniefile.h"
#include <spawn.h>
#include <poll.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <time.h>
extern char **environ;

static void reap(pid_t child)
{
    int status;
    pid_t done = waitpid(child, &status, WNOHANG);
    if (done == child || (done < 0 && errno == ECHILD))
        return;
    kill(child, SIGTERM);
    for (unsigned i = 0; i < 10; i++)
    {
        done = waitpid(child, &status, WNOHANG);
        if (done == child || (done < 0 && errno == ECHILD))
            return;
        struct timespec wait = {0, 50000000};
        nanosleep(&wait, NULL);
    }
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR)
    {
    }
}
static time_t monotonic_seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec;
}
/* A decoder pass retains only a fixed PCM buffer and bounded diagnostics.
 * Counting and encoding share identical arguments and a job-wide deadline. */
static bool decode(const char *path, toniefile_t *taf, bool chapter, uint64_t first, uint64_t last, uint64_t limit,
                   const char *output, const atomic_bool *cancel, time_t deadline, uint64_t *decoded,
                   uint64_t *retained)
{
    int pcm_pipe[2], diagnostics[2];
    if (pipe(pcm_pipe))
        return false;
    if (pipe(diagnostics))
    {
        close(pcm_pipe[0]);
        close(pcm_pipe[1]);
        return false;
    }
    for (unsigned i = 0; i < 2; i++)
    {
        fcntl(pcm_pipe[i], F_SETFD, FD_CLOEXEC);
        fcntl(diagnostics[i], F_SETFD, FD_CLOEXEC);
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pcm_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, diagnostics[1], STDERR_FILENO);
    for (unsigned i = 0; i < 2; i++)
    {
        posix_spawn_file_actions_addclose(&actions, pcm_pipe[i]);
        posix_spawn_file_actions_addclose(&actions, diagnostics[i]);
    }
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    char *argv[] = {"ffmpeg",
                    "-nostdin",
                    "-v",
                    "error",
                    "-protocol_whitelist",
                    "file,pipe",
                    "-format_whitelist",
                    "mp3,mov,ogg,flac,wav,aac",
                    "-i",
                    (char *)path,
                    "-map",
                    "0:a:0",
                    "-vn",
                    "-ac",
                    "2",
                    "-ar",
                    "48000",
                    "-f",
                    "s16le",
                    "pipe:1",
                    NULL};
    pid_t child;
    int spawned = posix_spawnp(&child, "ffmpeg", &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pcm_pipe[1]);
    close(diagnostics[1]);
    if (spawned)
    {
        close(pcm_pipe[0]);
        close(diagnostics[0]);
        return false;
    }
    fcntl(pcm_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(diagnostics[0], F_SETFL, O_NONBLOCK);
    struct pollfd pipes[2] = {{pcm_pipe[0], POLLIN, 0}, {diagnostics[0], POLLIN, 0}};
    int16_t pcm[8192];
    size_t buffered = 0, diagnostic_size = 0;
    char diagnostic[512];
    bool ok = true, chapter_started = !chapter;
    *decoded = *retained = 0;
    while (ok && (pipes[0].fd >= 0 || pipes[1].fd >= 0))
    {
        if (atomic_load(cancel) || monotonic_seconds() >= deadline)
        {
            ok = false;
            break;
        }
        int ready = poll(pipes, 2, 100);
        if (ready < 0)
        {
            if (errno == EINTR)
                continue;
            ok = false;
            break;
        }
        if (ready == 0)
            continue;
        for (unsigned i = 0; i < 2 && ok; i++)
        {
            if (pipes[i].fd < 0 || !pipes[i].revents)
                continue;
            char scratch[1024];
            ssize_t n =
                read(pipes[i].fd, i ? scratch : (char *)pcm + buffered, i ? sizeof(scratch) : sizeof(pcm) - buffered);
            if (n < 0)
            {
                if (errno != EAGAIN && errno != EINTR)
                    ok = false;
                continue;
            }
            if (!n)
            {
                close(pipes[i].fd);
                pipes[i].fd = -1;
                continue;
            }
            if (i)
            {
                size_t keep =
                    (size_t)n < sizeof(diagnostic) - diagnostic_size ? (size_t)n : sizeof(diagnostic) - diagnostic_size;
                memcpy(diagnostic + diagnostic_size, scratch, keep);
                diagnostic_size += keep;
                continue;
            }
            buffered += (size_t)n;
            size_t bytes = buffered & ~(size_t)3;
            uint64_t frames = bytes / 4, skip = 0;
            if (frames > limit - *decoded)
            {
                ok = false;
                break;
            }
            uint64_t selected = podcast_trim_overlap(*decoded, frames, first, last, &skip);
            *decoded += frames;
            if (taf && selected)
            {
                if (!chapter_started)
                {
                    if (toniefile_new_chapter(taf) != NO_ERROR)
                    {
                        ok = false;
                        break;
                    }
                    chapter_started = true;
                }
                if (toniefile_encode(taf, pcm + skip * 2, (size_t)selected) != NO_ERROR)
                {
                    ok = false;
                    break;
                }
                *retained += selected;
                struct stat st;
                if (stat(output, &st) || st.st_size > 240LL * 1024 * 1024)
                {
                    ok = false;
                    break;
                }
            }
            buffered -= bytes;
            memmove(pcm, (char *)pcm + bytes, buffered);
        }
    }
    for (unsigned i = 0; i < 2; i++)
        if (pipes[i].fd >= 0)
            close(pipes[i].fd);
    int status = 0;
    bool ended = false;
    for (unsigned i = 0; ok && i < 20; i++)
    {
        pid_t done = waitpid(child, &status, WNOHANG);
        if (done == child)
        {
            ended = true;
            break;
        }
        if ((done < 0 && errno != EINTR) || atomic_load(cancel))
            break;
        struct timespec delay = {0, 50000000};
        nanosleep(&delay, NULL);
    }
    if (!ended)
        reap(child);
    /* Captured diagnostics deliberately do not expose local paths or queries. */
    (void)diagnostic;
    return ok && ended && WIFEXITED(status) && !WEXITSTATUS(status) && !buffered && *decoded > 0;
}
bool podcast_encode(const podcast_encode_input_t *inputs, size_t count, const char *output, uint32_t audio_id,
                    const atomic_bool *cancel, double *durations, char *err, size_t size)
{
    toniefile_t *taf = toniefile_create(output, audio_id, false, 0);
    bool ok = taf != NULL;
    uint64_t source_frames = 0, maximum = 6ULL * 3600 * PODCAST_SAMPLE_RATE;
    size_t episode = 0;
    time_t deadline = monotonic_seconds() + 3600;
    for (; ok && episode < count; episode++)
    {
        const podcast_encode_input_t *input = &inputs[episode];
        uint64_t total = 0, retained = 0, first = 0, last = UINT64_MAX;
        if (input->trim_end_seconds)
        {
            uint64_t discarded;
            ok = decode(input->path, NULL, false, 0, 0, maximum - source_frames, output, cancel, deadline, &total,
                        &discarded) &&
                 podcast_trim_window(total, input->trim_start_seconds, input->trim_end_seconds, &first, &last);
            if (!ok)
                break;
        }
        else
        {
            if (!podcast_trim_frames(input->trim_start_seconds, &first))
            {
                ok = false;
                break;
            }
        }
        uint64_t measured = 0;
        ok = decode(input->path, taf, episode > 0, first, last, maximum - source_frames, output, cancel, deadline,
                    &measured, &retained) &&
             retained > 0 && (!input->trim_end_seconds || measured == total);
        source_frames += measured;
        if (!ok)
            break;
        if (ok)
            durations[episode] = (double)retained / PODCAST_SAMPLE_RATE;
    }
    if (taf && toniefile_close(taf) != NO_ERROR)
        ok = false;
    if (ok)
    {
        tonie_info_t *info = getTonieInfoV2(output, false, true, get_settings());
        ok = info && info->valid && info->tafHeader && info->tafHeader->n_track_page_nums == count &&
             isValidTaf(output, true);
        if (info)
            freeTonieInfo(info);
        struct stat st;
        ok = ok && !stat(output, &st) && st.st_size <= 240LL * 1024 * 1024;
    }
    if (!ok)
    {
        remove(output);
        snprintf(err, size, "Episode %zu: decoding/cuts failed, was cancelled, or exceeded preparation limits",
                 episode + 1);
    }
    return ok;
}
#endif
