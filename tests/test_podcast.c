#include "podcast_feed.h"
#include "podcast_http.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cJSON *parse(const char *feed, unsigned n, bool oldest)
{
    char error[256];
    return podcast_feed_parse(feed, strlen(feed), "https://example.org/feed", n, oldest, 2000000000, error,
                              sizeof(error));
}
int main(void)
{
    assert(podcast_http_init());
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
    char oversized_error[256];
    assert(!podcast_feed_parse(feed, PODCAST_FEED_LIMIT + 1, "https://example.org/feed", 1, false, 2000000000,
                               oversized_error, sizeof(oversized_error)));
    cJSON *config = cJSON_Parse("{\"feedUrl\":\"https://example.org/"
                                "rss\",\"episodeCount\":5,\"order\":\"newest_first\",\"intervalHours\":6,\"enabled\":"
                                "true,\"refreshOnPlacement\":false}");
    char error[256];
    assert(podcast_config_valid(config, error, sizeof(error)));
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
