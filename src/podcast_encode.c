#include "podcast.h"
#if PODCAST_SUPPORT
#include "podcast_encode.h"
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
bool podcast_encode(const char *const *inputs, size_t count, const char *output, uint32_t audio_id,
                    const atomic_bool *cancel, char *err, size_t size)
{
    toniefile_t *taf = toniefile_create(output, audio_id, false, 0);
    bool ok = taf != NULL;
    uint64_t frames = 0;
    time_t deadline = monotonic_seconds() + 3600;
    for (size_t i = 0; ok && i < count; i++)
    {
        if (i && toniefile_new_chapter(taf) != NO_ERROR)
        {
            ok = false;
            break;
        }
        int pipes[2];
        if (pipe(pipes))
        {
            ok = false;
            break;
        }
        fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
        fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipes[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipes[0]);
        posix_spawn_file_actions_addclose(&actions, pipes[1]);
        posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
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
                        (char *)inputs[i],
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
        int spawn_error = posix_spawnp(&child, "ffmpeg", &actions, NULL, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        close(pipes[1]);
        if (spawn_error)
        {
            close(pipes[0]);
            ok = false;
            break;
        }
        fcntl(pipes[0], F_SETFL, O_NONBLOCK);
        int16_t pcm[8192];
        size_t buffered = 0;
        uint64_t before = frames;
        bool eof = false;
        while (ok && !eof)
        {
            if (atomic_load(cancel) || monotonic_seconds() > deadline)
            {
                ok = false;
                break;
            }
            struct pollfd p = {pipes[0], POLLIN, 0};
            int ready = poll(&p, 1, 100);
            if (ready < 0 && errno != EINTR)
            {
                ok = false;
                break;
            }
            if (ready <= 0)
                continue;
            ssize_t n = read(pipes[0], (char *)pcm + buffered, sizeof(pcm) - buffered);
            if (n < 0)
            {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                ok = false;
                break;
            }
            if (!n)
            {
                eof = true;
                break;
            }
            buffered += (size_t)n;
            size_t bytes = buffered & ~(size_t)3;
            frames += bytes / 4;
            if (frames > (uint64_t)6 * 3600 * 48000 || toniefile_encode(taf, pcm, bytes / 4) != NO_ERROR)
            {
                ok = false;
                break;
            }
            buffered -= bytes;
            memmove(pcm, (char *)pcm + bytes, buffered);
            struct stat info;
            if (!stat(output, &info) && info.st_size > 240LL * 1024 * 1024)
            {
                ok = false;
                break;
            }
        }
        close(pipes[0]);
        int status = 0;
        bool ended = false;
        for (unsigned j = 0; ok && j < 20; j++)
        {
            pid_t done = waitpid(child, &status, WNOHANG);
            if (done == child)
            {
                ended = true;
                break;
            }
            if (done < 0 && errno != EINTR)
                break;
            if (atomic_load(cancel))
                break;
            struct timespec wait = {0, 50000000};
            nanosleep(&wait, NULL);
        }
        if (!ended)
            reap(child);
        if (!ended || !WIFEXITED(status) || WEXITSTATUS(status) || buffered || frames == before)
            ok = false;
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
        snprintf(err, size, "Audio preparation failed, was cancelled, or exceeded the six-hour/240 MiB limit");
    }
    return ok;
}
#endif
