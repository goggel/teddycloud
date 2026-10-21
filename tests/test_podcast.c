#include "podcast_feed.h"
#include "podcast_http.h"
#include "podcast_ard.h"
#include "podcast_trim.h"
#include <arpa/inet.h>
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *parse(const char *feed, unsigned n, bool oldest)
{
    assert(!podcast_ard_identifier("https://ardsounds.de/x/../sendung/x/123", (char[64]){0}));
    time_t invalid_date;
    assert(!podcast_ard_date("2026-01-01T-1:00:00Z", &invalid_date));
    assert(!podcast_ard_date("2026-01-01T01:00:00+-1:00", &invalid_date));
    assert(!podcast_ard_date("2026-01-01T01:00:00+01:-1", &invalid_date));
    char error[256];
    return podcast_feed_parse(feed, strlen(feed), "https://example.org/feed", n, oldest, 2000000000, error,
                              sizeof(error));
}
static void expect_feed_error(const char *feed, const char *message)
{
    char error[256] = "";
    cJSON *result = podcast_feed_parse(feed, feed ? strlen(feed) : 0, "https://example.org/feed", 1, false,
                                      2000000000, error, sizeof(error));
    assert(!result);
    assert(strstr(error, message));
}
static void test_feed_media(void)
{
    const char *video = "<rss><channel><item><enclosure type='video/mpeg' url='/maus.mp4'/></item></channel></rss>";
    assert(podcast_feed_video_only(video, strlen(video), "https://example.org/feed"));
    const char *unknown_or_audio[] = {
        "",
        "<broken",
        "<html/>",
        "<rss><channel/></rss>",
        "<!DOCTYPE rss><rss><channel><item><enclosure type='video/mp4' url='/a.mp4'/></item></channel></rss>",
        "<rss><channel><item><enclosure type='video/mp4' url='file:///a.mp4'/></item></channel></rss>",
        "<rss><channel><item><enclosure type='video/mp4'/></item></channel></rss>",
        "<rss><channel><item><enclosure type='audio/mpeg' url='/a.mp3'/></item></channel></rss>",
        "<rss><channel><item><enclosure type='video/mp4' url='/a.mp4'/></item>"
        "<item><enclosure type='audio/mpeg' url='/a.mp3'/></item></channel></rss>",
        "<rss><channel><item><enclosure type='video/mp4' url='/a.mp4'/>"
        "<enclosure type='application/ogg' url='/a.ogg'/></item></channel></rss>",
        NULL};
    for (unsigned i = 0; unknown_or_audio[i]; i++)
        assert(!podcast_feed_video_only(unknown_or_audio[i], strlen(unknown_or_audio[i]), "https://example.org/feed"));
    assert(!podcast_feed_video_only(NULL, 0, "https://example.org/feed"));
    assert(!podcast_feed_video_only(video, PODCAST_FEED_LIMIT + 1, "https://example.org/feed"));
}
static void test_display_name_validation(void)
{
    cJSON *config = cJSON_Parse("{\"feedUrl\":\"https://example.org/feed\",\"episodeCount\":1,"
                                 "\"order\":\"newest_first\",\"intervalHours\":0,"
                                 "\"enabled\":true,\"refreshOnPlacement\":true}");
    char error[256];
    assert(config && podcast_config_valid(config, error, sizeof(error)));
    cJSON_AddStringToObject(config, "displayName", "Podcast 🎵");
    cJSON_AddStringToObject(config, "displayNameSourceKey", "rss:https://example.org/feed");
    assert(podcast_config_valid(config, error, sizeof(error)));
    cJSON_ReplaceItemInObject(config, "displayName", cJSON_CreateString("bad\nname"));
    assert(!podcast_config_valid(config, error, sizeof(error)));
    cJSON_ReplaceItemInObject(config, "displayName", cJSON_CreateString("Name"));
    cJSON_ReplaceItemInObject(config, "displayNameSourceKey", cJSON_CreateNumber(1));
    assert(!podcast_config_valid(config, error, sizeof(error)));
    cJSON_Delete(config);
}
static void test_fractional_trim(void)
{
    uint64_t frames, first, last;
    assert(podcast_trim_frames(21.5, &frames) && frames == 1032000);
    assert(podcast_trim_frames(0.001, &frames) && frames == 48);
    assert(podcast_trim_frames(0.00002, &frames) && frames == 1);
    const double invalid[] = {-0.001, NAN, INFINITY, 21600.001};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    {
        assert(!podcast_trim_frames(invalid[i], &frames));
        assert(!podcast_trim_window(12 * 48000, invalid[i], 0, &first, &last));
        assert(!podcast_trim_window(12 * 48000, 0, invalid[i], &first, &last));
    }
    assert(podcast_trim_window(12 * 48000, 3.125, 3.25, &first, &last));
    assert(first == 150000 && last == 420000);
    assert(!podcast_trim_window(12 * 48000, 5.5, 6.5, &first, &last));

    cJSON *config = cJSON_Parse("{\"trimStartSeconds\":21.5,\"trimEndSeconds\":0.125,"
                                "\"episodeTrims\":[{\"identity\":\"a\",\"trimStartSeconds\":0.001,"
                                "\"trimEndSeconds\":1.75}]}");
    char error[256];
    double start, end;
    assert(config && podcast_trim_valid(config, error, sizeof(error)));
    podcast_trim_effective(config, "b", &start, &end);
    assert(start == 21.5 && end == 0.125);
    podcast_trim_effective(config, "a", &start, &end);
    assert(start == 0.001 && end == 1.75);
    cJSON *preview = cJSON_Parse("{\"episodes\":[{\"identity\":\"b\",\"durationSeconds\":30}],\"warnings\":[]}");
    podcast_trim_preview(preview, config);
    cJSON *episode = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(preview, "episodes"), 0);
    assert(podcast_number(episode, "trimStartSeconds") == 21.5);
    assert(podcast_number(episode, "estimatedTrimmedDurationSeconds") == 8.375);
    cJSON_Delete(preview);
    cJSON_Delete(config);
}
int main(void)
{
    assert(podcast_http_init());
    test_feed_media();
    test_display_name_validation();
    test_fractional_trim();
    char identifier[64];
    assert(podcast_ard_identifier("https://ardaudiothek.de/sendung/show/23667030/?share=1", identifier));
    assert(!strcmp(identifier, "23667030"));
    assert(podcast_ard_identifier("https://www.ardsounds.de/sendung/show/urn%3Aard%3Ashow%3A0e3f60ed4ba198df/",
                                  identifier));
    assert(!strcmp(identifier, "urn:ard:show:0e3f60ed4ba198df"));
    const char *bad_ard[] = {"https://ardsounds.de.evil.org/sendung/show/123",
                             "http://ardsounds.de/sendung/show/123",
                             "https://ardsounds.de/sendung/show/123/extra",
                             "https://ardsounds.de/sendung/show/123%2f",
                             "https://ardsounds.de/sendung/show/123%252f",
                             "https://ardsounds.de:444/sendung/show/123",
                             NULL};
    for (unsigned i = 0; bad_ard[i]; i++)
        assert(!podcast_ard_identifier(bad_ard[i], identifier));
    time_t t1, t2;
    assert(podcast_ard_date("2026-10-09T19:04:00+02:00", &t1));
    assert(podcast_ard_date("2026-10-09T17:04:00Z", &t2) && t1 == t2);
    assert(podcast_ard_date("2026-10-09T16:04:00-01:00", &t2) && t1 == t2);
    assert(!podcast_ard_date("2026-02-30T17:04:00Z", &t2));
    assert(!podcast_ard_date("2026-10-09T17:04:00", &t2));
    uint64_t first, last, skip;
    assert(podcast_trim_window(12 * 48000, 3, 3, &first, &last) && last - first == 6 * 48000);
    assert(!podcast_trim_window(12 * 48000, 6, 6, &first, &last));
    assert(!podcast_trim_window(12 * 48000, 13, 0, &first, &last));
    assert(podcast_trim_overlap(2 * 48000, 2 * 48000, 3 * 48000, 9 * 48000, &skip) == 48000 && skip == 48000);
    assert(!podcast_trim_overlap(UINT64_MAX - 1, 4, 0, UINT64_MAX, &skip));
    assert(podcast_url_valid("https://example.org/feed?x=y"));
    const char *bad[] = {"file:///etc/passwd",
                         "ftp://example.org/a",
                         "https://user:pass@example.org",
                         "https://example.org/#fragment",
                         "https://example.org/\r\nx",
                         NULL};
    for (unsigned i = 0; bad[i]; i++)
        assert(!podcast_url_valid(bad[i]));
    const char *private4[] = {"127.0.0.1",  "10.2.3.4", "172.16.1.2", "192.168.1.1", "169.254.169.254",
                              "100.64.0.1", "0.0.0.0",  "224.0.0.1",  "198.18.0.2",  NULL};
    for (unsigned i = 0; private4[i]; i++)
    {
        struct in_addr a;
        assert(inet_pton(AF_INET, private4[i], &a) == 1);
        assert(!podcast_address_public(AF_INET, &a));
    }
    struct in_addr public;
    inet_pton(AF_INET, "8.8.8.8", &public);
    assert(podcast_address_public(AF_INET, &public));
    const char *private6[] = {"::1", "::ffff:8.8.8.8", "fc00::1", "fe80::1", "2001:db8::1", "2002::1", NULL};
    for (unsigned i = 0; private6[i]; i++)
    {
        struct in6_addr a;
        inet_pton(AF_INET6, private6[i], &a);
        assert(!podcast_address_public(AF_INET6, &a));
    }
    const char *feed =
        "<rss version='2.0' xmlns:itunes='http://www.itunes.com/dtds/podcast-1.0.dtd'><channel><title><![CDATA[Test & "
        "title]]></title>"
        "<item><title>Undated</title><guid>u</guid><enclosure type='audio/mpeg' url='/u.mp3'/></item>"
        "<item><title>Old</title><guid>o</guid><pubDate>Thu, 01 Jan 2026 09:00:00 +0100</pubDate><enclosure "
        "type='audio/mpeg' url='/o.mp3'/></item>"
        "<item><title>New</title><guid>n</guid><pubDate>Thu, 01 Jan 2026 09:30:00 +0000</pubDate><enclosure "
        "type='audio/mpeg' url='/n.mp3'/></item>"
        "<item><title>Duplicate</title><guid>n</guid><enclosure type='audio/mpeg' url='/dup.mp3'/></item>"
        "<item><title>Video</title><enclosure type='video/mp4' url='/video'/></item>"
        "<item><title>Future</title><pubDate>Thu, 01 Jan 2099 09:00:00 +0000</pubDate><enclosure type='audio/mpeg' "
        "url='/future'/></item>"
        "</channel></rss>";
    cJSON *p = parse(feed, 2, false);
    assert(p);
    assert(!strcmp(podcast_string(p, "title"), "Test & title"));
    cJSON *e = cJSON_GetObjectItemCaseSensitive(p, "episodes");
    assert(cJSON_GetArraySize(e) == 2);
    assert(!strcmp(podcast_string(cJSON_GetArrayItem(e, 0), "identity"), "n"));
    assert(!strcmp(podcast_string(cJSON_GetArrayItem(e, 1), "url"), "https://example.org/o.mp3"));
    cJSON_Delete(p);
    p = parse(feed, 2, true);
    assert(p);
    e = cJSON_GetObjectItemCaseSensitive(p, "episodes");
    assert(!strcmp(podcast_string(cJSON_GetArrayItem(e, 0), "identity"), "o"));
    cJSON_Delete(p);
    p = parse(feed, 50, false);
    assert(p);
    e = cJSON_GetObjectItemCaseSensitive(p, "episodes");
    assert(cJSON_GetArraySize(e) == 3);
    assert(!strcmp(podcast_string(cJSON_GetArrayItem(e, 2), "identity"), "u"));
    assert(cJSON_GetArraySize(cJSON_GetObjectItemCaseSensitive(p, "warnings")) == 1);
    cJSON_Delete(p);
    assert(!parse("<!DOCTYPE rss [<!ENTITY test 'secret'>]><rss><channel><title>&test;</title></channel></rss>", 1,
                  false));
    assert(!parse("<feed xmlns='http://www.w3.org/2005/Atom'/>", 1, false));
    assert(!parse("<rss><channel/></rss>", 1, false));
    assert(!parse("<broken", 1, false));
    assert(!parse(feed, 0, false));
    expect_feed_error(NULL, "empty feed");
    expect_feed_error("", "empty feed");
    expect_feed_error("<broken", "valid XML");
    expect_feed_error("<html><body>Podcast homepage</body></html>", "podcast webpage");
    expect_feed_error("<feed xmlns='http://www.w3.org/2005/Atom'/>", "does not return an RSS feed");
    expect_feed_error("<rss/>", "no channel");
    expect_feed_error("<!DOCTYPE rss><rss><channel/></rss>", "DTD or entity");
    expect_feed_error("<rss><channel><item><enclosure type='video/mp4' url='/video'/></item></channel></rss>",
                      "video episodes but no playable audio episodes");
    /* WDR's Sendung mit der Maus feed advertises MP4 files as video/mpeg. */
    expect_feed_error("<rss version='2.0'><channel><title>Die Sendung mit der Maus</title><item>"
                      "<enclosure type='video/mpeg' url='https://example.org/maus_AVC-270.mp4'/>"
                      "</item></channel></rss>", "use an audio podcast feed");
    expect_feed_error("<rss><channel><item><pubDate>Thu, 01 Jan 2099 09:00:00 +0000</pubDate>"
                      "<enclosure type='audio/mpeg' url='/future.mp3'/></item></channel></rss>",
                      "no published episodes");
    const char *repeated_item = "<item><enclosure type='audio/mpeg' url='/same.mp3'/></item>";
    char *large_feed = calloc(1, 1001 * strlen(repeated_item) + 64);
    assert(large_feed);
    strcpy(large_feed, "<rss><channel>");
    for (unsigned i = 0; i < 1000; i++)
        strcat(large_feed, repeated_item);
    size_t channel_end = strlen(large_feed);
    strcpy(large_feed + channel_end, "</channel></rss>");
    p = parse(large_feed, 1, false);
    assert(p);
    cJSON_Delete(p);
    strcpy(large_feed + channel_end, repeated_item);
    strcat(large_feed, "</channel></rss>");
    expect_feed_error(large_feed, "1,000 episode limit");
    free(large_feed);
    char unicode_feed[1024];
    char title[516];
    memset(title, 'a', 511);
    strcpy(title + 511, "\xe2\x82\xac");
    snprintf(unicode_feed, sizeof(unicode_feed),
             "<rss><channel><title>%s</title><item><enclosure type='audio/mpeg' url='/a.mp3'/></item></channel></rss>",
             title);
    p = parse(unicode_feed, 1, false);
    assert(p && strlen(podcast_string(p, "title")) == 511);
    cJSON_Delete(p);
    char oversized_error[256];
    assert(!podcast_feed_parse(feed, PODCAST_FEED_LIMIT + 1, "https://example.org/feed", 1, false, 2000000000,
                               oversized_error, sizeof(oversized_error)));
    assert(strstr(oversized_error, "5 MiB size limit"));
    cJSON *config = cJSON_Parse("{\"feedUrl\":\"https://example.org/"
                                "rss\",\"episodeCount\":5,\"order\":\"newest_first\",\"intervalHours\":6,\"enabled\":"
                                "true,\"refreshOnPlacement\":false}");
    assert(!podcast_ard_identifier("https://ardsounds.de/x/../sendung/x/123", (char[64]){0}));
    time_t invalid_date;
    assert(!podcast_ard_date("2026-01-01T-1:00:00Z", &invalid_date));
    assert(!podcast_ard_date("2026-01-01T01:00:00+-1:00", &invalid_date));
    assert(!podcast_ard_date("2026-01-01T01:00:00+01:-1", &invalid_date));
    char error[256];
    assert(podcast_config_valid(config, error, sizeof(error)));
    cJSON_DeleteItemFromObjectCaseSensitive(config, "refreshOnPlacement");
    assert(podcast_config_valid(config, error, sizeof(error)));
    cJSON_AddStringToObject(config, "refreshOnPlacement", "false");
    assert(!podcast_config_valid(config, error, sizeof(error)));
    cJSON_DeleteItemFromObjectCaseSensitive(config, "refreshOnPlacement");
    cJSON_SetNumberValue(cJSON_GetObjectItemCaseSensitive(config, "episodeCount"), 1.5);
    assert(!podcast_config_valid(config, error, sizeof(error)));
    cJSON_Delete(config);
    /* Production transport cannot connect to local destinations. */
    podcast_response_t response;
    atomic_bool cancel = false;
    assert(!podcast_http_get("http://127.0.0.1/", 1024, NULL, &cancel, &response, error, sizeof(error)));
    podcast_http_cleanup();
    puts("Podcast parser/selection/network-policy tests passed");
    return 0;
}
