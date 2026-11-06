#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "nct_music.h"
#include "display_text.h"

static cJSON *song(void)
{
    return cJSON_Parse("{\"key\":\"utXSBhTqw0Jk\",\"name\":\"Như Một Người Rưng\","
        "\"artistName\":\"OSAD, Hà An Huy\",\"duration\":177,\"statusPlay\":1,"
        "\"streamURL\":[{\"type\":\"128\",\"status\":1,\"onlyVIP\":false,"
        "\"stream\":\"https://stream.nct.vn/test.mp3?st=example\"}]}");
}

static char *response(cJSON *data)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddItemToObject(root, "data", data);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    assert(json);
    return json;
}

static void check_detail(void)
{
    nct_track_t track;
    char url[NCT_URL_SIZE];
    cJSON *data = song();
    char *json = response(data);
    assert(nct_parse_detail(json, &track, url, sizeof(url)) == ESP_OK);
    assert(!strcmp(track.key, "utXSBhTqw0Jk") && track.duration == 177 && track.playable);
    assert(!strcmp(track.title, "Như Một Người Rưng"));
    assert(!strcmp(url, "https://stream.nct.vn/test.mp3?st=example"));
    assert(nct_parse_detail(json, &track, url, 4) == ESP_ERR_INVALID_SIZE);
    cJSON_free(json);
    for (int scenario = 0; scenario < 6; ++scenario) {
        data = song();
        cJSON *stream = cJSON_GetArrayItem(cJSON_GetObjectItem(data, "streamURL"), 0);
        if (scenario == 0) cJSON_ReplaceItemInObject(stream, "onlyVIP", cJSON_CreateTrue());
        if (scenario == 1) cJSON_ReplaceItemInObject(stream, "status", cJSON_CreateNumber(0));
        if (scenario == 2) cJSON_DeleteItemFromObject(stream, "onlyVIP");
        if (scenario == 3) cJSON_ReplaceItemInObject(data, "statusPlay", cJSON_CreateNumber(1.5));
        if (scenario == 4) cJSON_ReplaceItemInObject(stream, "stream", cJSON_CreateString("https://evil.test/x.mp3"));
        if (scenario == 5) cJSON_ReplaceItemInObject(stream, "type", cJSON_CreateString("lossless"));
        json = response(data);
        strcpy(url, "old");
        assert(nct_parse_detail(json, &track, url, sizeof(url)) == ESP_ERR_NOT_FOUND);
        assert(!url[0]);
        cJSON_free(json);
    }
    data = song();
    char long_title[160];
    memset(long_title, 'A', 126);
    strcpy(long_title + 126, "ộ");
    cJSON_ReplaceItemInObject(data, "name", cJSON_CreateString(long_title));
    json = response(data);
    assert(nct_parse_detail(json, &track, url, sizeof(url)) == ESP_OK);
    assert(strlen(track.title) == 126); // Never cut a UTF-8 codepoint in half.
    cJSON_free(json);
}

static void check_search(void)
{
    nct_track_t tracks[NCT_MAX_TRACKS];
    size_t count = NCT_MAX_TRACKS;
    cJSON *data = cJSON_CreateObject();
    cJSON *songs = cJSON_AddArrayToObject(data, "songs");
    cJSON *invalid = song();
    cJSON_DeleteItemFromObject(invalid, "key");
    cJSON_AddItemToArray(songs, invalid);
    for (int i = 0; i < 8; ++i) cJSON_AddItemToArray(songs, song());
    char *json = response(data);
    assert(nct_parse_search(json, tracks, &count) == ESP_OK && count == NCT_MAX_TRACKS);
    assert(tracks[0].playable);
    count = 1;
    assert(nct_parse_search(json, tracks, &count) == ESP_OK && count == 1);
    cJSON_free(json);
    count = NCT_MAX_TRACKS;
    assert(nct_parse_search("{\"code\":0,\"data\":{\"songs\":[]}}", tracks, &count) == ESP_OK && count == 0);
    const char *bad[] = {"garbage", "{}", "{\"code\":1}", "{\"code\":0.5,\"data\":{\"songs\":[]}}", "{\"code\":0,\"data\":{}}"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        count = NCT_MAX_TRACKS;
        assert(nct_parse_search(bad[i], tracks, &count) == ESP_ERR_INVALID_RESPONSE && count == 0);
    }
}

static void check_keys_and_urls(void)
{
    assert(nct_valid_key("utXSBhTqw0Jk"));
    const char *keys[] = {NULL,"","../song","a/b","a?x=1","é","abcdefghijklmnopqrstuvwxyz1234567"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) assert(!nct_valid_key(keys[i]));
    assert(nct_valid_stream_url("https://a01.nct.vn/a.mp3?st=abc&e=123"));
    assert(nct_valid_stream_url("https://stream.nixcdn.com/a.mp3"));
    const char *urls[] = {NULL,"http://a01.nct.vn/a", "https://a01.nct.vn.evil.test/a",
        "https://nct.vn@evil.test/a", "https://evil.test@a01.nct.vn/a", "https://a01.nct.vn:123/a",
        "https://a01.nct.vn/a\r\nHeader:x", "https://a01.nct.vn/a b"};
    for (size_t i = 0; i < sizeof(urls) / sizeof(urls[0]); ++i) assert(!nct_valid_stream_url(urls[i]));
}

static void check_display(void)
{
    char output[64];
    display_ascii("Như Một Người Rưng", output, sizeof(output));
    assert(!strcmp(output, "NHU MOT NGUOI RUNG"));
    display_ascii("Đặng Thùy — 123", output, sizeof(output));
    assert(!strcmp(output, "DANG THUY 123"));
    display_ascii("a\xcc\x82\xcc\x81", output, sizeof(output));
    assert(!strcmp(output, "A"));
    display_ascii("  xin   chào  ", output, 5);
    assert(!strcmp(output, "XIN"));
    display_ascii(NULL, output, sizeof(output));
    assert(!output[0]);
    output[0] = 'X';
    display_ascii("ABC", output, 0);
    assert(output[0] == 'X');
    display_ascii("ABC", output, 1);
    assert(!output[0]);
}

int main(void)
{
    check_detail();
    check_search();
    check_keys_and_urls();
    check_display();
    puts("Music API parsing, availability, URL validation and Vietnamese LCD text passed.");
    return 0;
}
