#if PODCAST_SUPPORT
#include "podcast_feed.h"
#include "podcast_http.h"
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
    bool valid = cJSON_IsObject(c) && podcast_url_valid(podcast_string(c, "feedUrl")) && n >= 1 && n <= 50 &&
                 floor(n) == n && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(c, "intervalHours")) && h >= 0 &&
                 h <= 168 && floor(h) == h && (!strcmp(order, "newest_first") || !strcmp(order, "oldest_first")) &&
                 cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(c, "enabled")) &&
                 cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(c, "refreshOnPlacement"));
    if (!valid)
        snprintf(err, size, "Use a public RSS URL, 1–50 episodes, a playback order and 0–168 whole hours");
    return valid;
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
cJSON *podcast_feed_parse(const char *xml, size_t length, const char *base, unsigned count, bool oldest, time_t now,
                          char *err, size_t size)
{
    cJSON *result = NULL;
    xmlDoc *doc = NULL;
    if (!xml || length > PODCAST_FEED_LIMIT || !count || count > 50)
        goto fail;
    /* Do not enable RECOVER, HUGE, NOENT or DTDLOAD. Reject declarations before
     * parsing as well as parsed DTDs: even internal entity expansion is unwanted. */
    if (contains(xml, length, "<!DOCTYPE") || contains(xml, length, "<!ENTITY"))
        goto fail;
    doc = xmlReadMemory(xml, (int)length, base, NULL, XML_PARSE_NONET | XML_PARSE_NOERROR | XML_PARSE_NOWARNING);
    xmlNode *rss = doc ? xmlDocGetRootElement(doc) : NULL, *channel = NULL;
    if (!rss || doc->intSubset || doc->extSubset || xmlStrcmp(rss->name, BAD_CAST "rss"))
        goto fail;
    for (xmlNode *n = rss->children; n; n = n->next)
        if (!xmlStrcmp(n->name, BAD_CAST "channel"))
            channel = n;
    if (!channel)
        goto fail;
    cJSON *items[1000];
    unsigned length_items = 0, seen = 0;
    for (xmlNode *n = channel->children; n; n = n->next)
    {
        if (xmlStrcmp(n->name, BAD_CAST "item"))
            continue;
        if (++seen > 1000)
        {
            for (unsigned i = 0; i < length_items; i++)
                cJSON_Delete(items[i]);
            goto fail;
        }
        char *title = text(n, "title"), *guid = text(n, "guid"), *date = text(n, "pubDate"), *url = NULL;
        time_t published = date[0] ? curl_getdate(date, NULL) : -1;
        for (xmlNode *e = n->children; e; e = e->next)
            if (!xmlStrcmp(e->name, BAD_CAST "enclosure"))
            {
                xmlChar *type = xmlGetProp(e, BAD_CAST "type"), *ref = xmlGetProp(e, BAD_CAST "url");
                if (type && ref &&
                    (!xmlStrncmp(type, BAD_CAST "audio/", 6) || !xmlStrcmp(type, BAD_CAST "application/ogg")))
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
                cJSON_AddNumberToObject(item, "duration", seconds);
            items[length_items++] = item;
        }
        free(title);
        free(guid);
        free(date);
        free(url);
    }
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
    snprintf(err, size,
             "Expected RSS 2.0 with playable audio enclosures (no DTD/entities, at most 1,000 items and 5 MiB)");
    return NULL;
}
#endif
