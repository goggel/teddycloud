#if PODCAST_SUPPORT
#include "podcast_feed.h"
#include "podcast_http.h"
#include "podcast_ard.h"
#include "podcast_trim.h"
#include <libxml/parser.h>
#include <libxml/uri.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

const char *podcast_string(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}
double podcast_number(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : 0;
}
bool podcast_config_valid(const cJSON *c, char *err, size_t size)
{
    double n = podcast_number(c, "episodeCount"), h = podcast_number(c, "intervalHours");
    const char *order = podcast_string(c, "order");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(c, "sourceType");
    char identifier[64];
    bool source = !type || (cJSON_IsString(type) && !strcmp(type->valuestring, "rss"))
                      ? podcast_url_valid(podcast_string(c, "feedUrl"))
                      : cJSON_IsString(type) && !strcmp(type->valuestring, "ard_audiothek") &&
                            podcast_ard_identifier(podcast_string(c, "ardShowUrl"), identifier);
    bool valid = cJSON_IsObject(c) && source && n >= 1 && n <= 50 && floor(n) == n &&
                 cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(c, "intervalHours")) && h >= 0 && h <= 168 &&
                 floor(h) == h && (!strcmp(order, "newest_first") || !strcmp(order, "oldest_first")) &&
                 cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(c, "enabled")) &&
                 cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(c, "refreshOnPlacement"));
    if (!valid)
        snprintf(err, size, "Use a public RSS URL, 1–50 episodes, a playback order and 0–168 whole hours");
    return valid && podcast_trim_valid(c, err, size);
}
static char *text(xmlNode *parent, const char *name)
{
    for (xmlNode *n = parent->children; n; n = n->next)
        if (n->type == XML_ELEMENT_NODE && !xmlStrcmp(n->name, BAD_CAST name))
        {
            xmlChar *v = xmlNodeGetContent(n);
            char *s = strdup(v ? (char *)v : "");
            xmlFree(v);
            if (strlen(s) > 512)
            {
                size_t end = 512;
                while (end && ((unsigned char)s[end] & 0xc0) == 0x80)
                    end--;
                s[end] = 0;
            }
            for (char *p = s; *p; p++)
                if ((unsigned char)*p < 32)
                    *p = ' ';
            return s;
        }
    return strdup("");
}
static unsigned duration_seconds(const char *value)
{
    unsigned long total = 0;
    unsigned parts = 0;
    const char *p = value;
    while (*p)
    {
        char *end = NULL;
        if (*p < '0' || *p > '9')
            return 0;
        unsigned long n = strtoul(p, &end, 10);
        if (n > 21600 || ++parts > 3 || (parts > 1 && n >= 60) || total > 21600 / 60)
            return 0;
        total = total * 60 + n;
        if (*end == ':')
            p = end + 1;
        else if (!*end)
            break;
        else
            return 0;
        if (!*p)
            return 0;
    }
    return total <= 21600 ? (unsigned)total : 0;
}
static int compare(const void *a, const void *b)
{
    double x = podcast_number(*(cJSON *const *)a, "publishedAt"), y = podcast_number(*(cJSON *const *)b, "publishedAt");
    if (x != y)
        return x > y ? -1 : 1;
    return (int)(podcast_number(*(cJSON *const *)a, "feedIndex") - podcast_number(*(cJSON *const *)b, "feedIndex"));
}
static bool contains(const char *data, size_t length, const char *pattern)
{
    size_t n = strlen(pattern);
    for (size_t i = 0; i + n <= length; i++)
        if (!memcmp(data + i, pattern, n))
            return true;
    return false;
}
static xmlDoc *read_feed(const char *xml, size_t length, const char *base, xmlNode **channel, const char **failure)
{
    xmlDoc *doc = NULL;
    *channel = NULL;
    *failure = "The podcast server returned an empty feed";
    if (!xml || !length)
        goto fail;
    *failure = "The podcast feed exceeds the 5 MiB size limit";
    if (length > PODCAST_FEED_LIMIT)
        goto fail;
    /* Do not enable RECOVER, HUGE, NOENT or DTDLOAD. Reject declarations before
     * parsing as well as parsed DTDs: even internal entity expansion is unwanted. */
    *failure = "The podcast feed contains a DTD or entity declaration, which is not supported";
    if (contains(xml, length, "<!DOCTYPE") || contains(xml, length, "<!ENTITY"))
        goto fail;
    doc = xmlReadMemory(xml, (int)length, base, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    *failure = "The podcast server did not return valid XML; check that the URL points to an RSS feed";
    if (!doc)
        goto fail;
    *failure = "The podcast feed contains a DTD or entity declaration, which is not supported";
    if (doc->intSubset || doc->extSubset)
        goto fail;
    xmlNode *rss = xmlDocGetRootElement(doc);
    *failure = "The URL does not return an RSS feed; use the RSS feed URL instead of a podcast webpage";
    if (!rss || xmlStrcmp(rss->name, BAD_CAST "rss"))
        goto fail;
    for (xmlNode *n = rss->children; n; n = n->next)
        if (!xmlStrcmp(n->name, BAD_CAST "channel"))
            *channel = n;
    *failure = "The RSS feed has no channel element";
    if (!*channel)
        goto fail;
    return doc;
fail:
    if (doc)
        xmlFreeDoc(doc);
    return NULL;
}
static bool audio_type(const xmlChar *type)
{
    return type && (!xmlStrncmp(type, BAD_CAST "audio/", 6) || !xmlStrcmp(type, BAD_CAST "application/ogg"));
}
bool podcast_feed_video_only(const char *xml, size_t length, const char *base)
{
    const char *failure;
    xmlNode *channel;
    xmlDoc *doc = read_feed(xml, length, base, &channel, &failure);
    if (!doc)
        return false;
    bool audio = false, video = false;
    unsigned seen = 0;
    for (xmlNode *n = channel->children; n; n = n->next)
    {
        if (xmlStrcmp(n->name, BAD_CAST "item"))
            continue;
        if (++seen > 1000)
        {
            video = false;
            break;
        }
        for (xmlNode *e = n->children; e; e = e->next)
            if (!xmlStrcmp(e->name, BAD_CAST "enclosure"))
            {
                xmlChar *type = xmlGetProp(e, BAD_CAST "type"), *ref = xmlGetProp(e, BAD_CAST "url");
                bool is_audio = audio_type(type), is_video = type && !xmlStrncmp(type, BAD_CAST "video/", 6);
                xmlChar *absolute = ref && (is_audio || is_video) ? xmlBuildURI(ref, BAD_CAST base) : NULL;
                if (absolute && podcast_url_valid((char *)absolute))
                {
                    audio |= is_audio;
                    video |= is_video;
                }
                xmlFree(absolute);
                xmlFree(type);
                xmlFree(ref);
            }
    }
    xmlFreeDoc(doc);
    return video && !audio;
}
cJSON *podcast_feed_parse(const char *xml, size_t length, const char *base, unsigned count, bool oldest, time_t now,
                          char *err, size_t size)
{
    cJSON *result = NULL;
    xmlDoc *doc = NULL;
    xmlNode *channel;
    const char *failure = "Invalid episode count: select 1–50 episodes";
    if (!count || count > 50)
        goto fail;
    doc = read_feed(xml, length, base, &channel, &failure);
    if (!doc)
        goto fail;
    cJSON *items[1000];
    unsigned length_items = 0, seen = 0;
    bool video_enclosures = false;
    for (xmlNode *n = channel->children; n; n = n->next)
    {
        if (xmlStrcmp(n->name, BAD_CAST "item"))
            continue;
        if (++seen > 1000)
        {
            for (unsigned i = 0; i < length_items; i++)
                cJSON_Delete(items[i]);
            failure = "The podcast feed exceeds the 1,000 episode limit";
            goto fail;
        }
        char *title = text(n, "title"), *guid = text(n, "guid"), *date = text(n, "pubDate"), *url = NULL;
        time_t published = date[0] ? curl_getdate(date, NULL) : -1;
        for (xmlNode *e = n->children; e; e = e->next)
            if (!xmlStrcmp(e->name, BAD_CAST "enclosure"))
            {
                xmlChar *type = xmlGetProp(e, BAD_CAST "type"), *ref = xmlGetProp(e, BAD_CAST "url");
                if (type && ref && !xmlStrncmp(type, BAD_CAST "video/", 6))
                    video_enclosures = true;
                if (ref && audio_type(type))
                {
                    xmlChar *absolute = xmlBuildURI(ref, BAD_CAST base);
                    if (absolute && podcast_url_valid((char *)absolute))
                    {
                        free(url);
                        url = strdup((char *)absolute);
                    }
                    xmlFree(absolute);
                }
                xmlFree(type);
                xmlFree(ref);
            }
        bool duplicate = false;
        for (unsigned i = 0; url && i < length_items; i++)
            if (!strcmp(podcast_string(items[i], "identity"), guid[0] ? guid : url) ||
                !strcmp(podcast_string(items[i], "url"), url))
                duplicate = true;
        if (url && !duplicate && published <= now)
        {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "identity", guid[0] ? guid : url);
            cJSON_AddStringToObject(item, "title", title[0] ? title : "Episode");
            cJSON_AddStringToObject(item, "url", url);
            cJSON_AddNumberToObject(item, "publishedAt", published < 0 ? 0 : published);
            cJSON_AddNumberToObject(item, "feedIndex", seen);
            char *duration = text(n, "duration");
            unsigned seconds = duration_seconds(duration);
            free(duration);
            if (seconds)
                cJSON_AddNumberToObject(item, "durationSeconds", seconds);
            items[length_items++] = item;
        }
        free(title);
        free(guid);
        free(date);
        free(url);
    }
    failure = video_enclosures
                  ? "This feed contains video episodes but no playable audio episodes; use an audio podcast feed"
                  : "The RSS feed has no published episodes with supported public audio enclosures";
    if (!length_items)
        goto fail;
    qsort(items, length_items, sizeof(*items), compare);
    unsigned selected = length_items < count ? length_items : count;
    result = cJSON_CreateObject();
    char *title = text(channel, "title");
    cJSON_AddStringToObject(result, "title", title);
    free(title);
    cJSON *episodes = cJSON_AddArrayToObject(result, "episodes");
    for (unsigned i = 0; i < selected; i++)
    {
        unsigned index = oldest ? selected - 1 - i : i;
        cJSON_DeleteItemFromObjectCaseSensitive(items[index], "feedIndex");
        cJSON_AddItemToArray(episodes, items[index]);
    }
    for (unsigned i = selected; i < length_items; i++)
        cJSON_Delete(items[i]);
    cJSON *warnings = cJSON_AddArrayToObject(result, "warnings");
    if (selected < count)
        cJSON_AddItemToArray(warnings, cJSON_CreateString("Fewer episodes are available than requested"));
    xmlFreeDoc(doc);
    return result;
fail:
    if (doc)
        xmlFreeDoc(doc);
    snprintf(err, size, "%s", failure);
    return NULL;
}
#endif
