#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "live_tools.h"
#include "music.h"
#include "robot_face.h"
#include "speaker.h"

typedef struct { unsigned epoch; char *json; bool hold_voice; } message_t;
static QueueHandle_t s_jobs, s_replies;
static atomic_uint s_epoch;
static atomic_bool s_failed;
static portMUX_TYPE s_cancel_lock = portMUX_INITIALIZER_UNLOCKED;
static char s_cancelled[16][257];
static unsigned s_cancel_count;

static bool valid_call(const char *id, unsigned epoch)
{
    bool valid = epoch == atomic_load(&s_epoch);
    portENTER_CRITICAL(&s_cancel_lock);
    for (unsigned i = 0; valid && i < s_cancel_count; ++i)
        if (!strcmp(s_cancelled[i], id)) valid = false;
    portEXIT_CRITICAL(&s_cancel_lock);
    return valid;
}

void live_tools_reset(void)
{
    atomic_fetch_add(&s_epoch, 1);
    atomic_store(&s_failed, false);
    portENTER_CRITICAL(&s_cancel_lock);
    s_cancel_count = 0;
    portEXIT_CRITICAL(&s_cancel_lock);
}

void live_tools_cancel(cJSON *cancellation)
{
    cJSON *id;
    cJSON_ArrayForEach(id, cJSON_GetObjectItemCaseSensitive(cancellation, "ids")) {
        if (!cJSON_IsString(id) || strlen(id->valuestring) > 256) continue;
        portENTER_CRITICAL(&s_cancel_lock);
        bool full = s_cancel_count == 16;
        if (!full) snprintf(s_cancelled[s_cancel_count++], sizeof(s_cancelled[0]), "%s", id->valuestring);
        portEXIT_CRITICAL(&s_cancel_lock);
        if (full) { live_tools_reset(); break; }
    }
}

bool live_tools_submit(cJSON *tool_call)
{
    if (!tool_call) return true;
    cJSON *calls = cJSON_GetObjectItemCaseSensitive(tool_call, "functionCalls");
    if (!cJSON_IsArray(calls) || cJSON_GetArraySize(calls) > 16) return false;
    if (!cJSON_GetArraySize(calls)) return true;
    cJSON *call;
    cJSON_ArrayForEach(call, calls) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(call, "id");
        cJSON *name = cJSON_GetObjectItemCaseSensitive(call, "name");
        if (!cJSON_IsString(id) || !id->valuestring[0] || strlen(id->valuestring) > 256 ||
            !cJSON_IsString(name) || !name->valuestring[0] || strlen(name->valuestring) > 64) return false;
    }
    message_t job = {.epoch = atomic_load(&s_epoch), .json = cJSON_PrintUnformatted(tool_call)};
    if (job.json && s_jobs && xQueueSend(s_jobs, &job, 0) == pdTRUE) return true;
    cJSON_free(job.json);
    return false;
}

bool live_tools_failed(void) { return atomic_load(&s_failed); }

bool live_tools_take_reply(char **json)
{
    message_t reply;
    while (s_replies && xQueueReceive(s_replies, &reply, 0) == pdTRUE) {
        if (reply.epoch == atomic_load(&s_epoch)) {
            if (reply.hold_voice) speaker_set_voice_pending(true);
            *json = reply.json;
            return true;
        }
        cJSON_free(reply.json);
    }
    return false;
}

static const char *state_name(music_state_t state)
{
    switch (state) {
    case MUSIC_BUFFERING: return "buffering";
    case MUSIC_PLAYING: return "playing";
    case MUSIC_PAUSED: return "paused";
    case MUSIC_ERROR: return "error";
    default: return "idle";
    }
}

static const char *execute(const char *name, cJSON *args, cJSON *result, const char *id, unsigned epoch)
{
    if (!strcmp(name, "set_expression")) {
        cJSON *emotion = cJSON_GetObjectItemCaseSensitive(args, "emotion");
        if (!robot_face_is_available()) return "display_unavailable";
        if (!cJSON_IsString(emotion) || !robot_face_set_emotion(emotion->valuestring)) return "invalid_emotion";
        cJSON_AddStringToObject(result, "status", "accepted");
        cJSON_AddStringToObject(result, "emotion", emotion->valuestring);
        cJSON_AddNumberToObject(result, "duration_ms", 15000);
        return NULL;
    }
    if (!music_available()) return "music_unavailable";
    if (!strcmp(name, "search_music")) {
        cJSON *query = cJSON_GetObjectItemCaseSensitive(args, "query");
        if (!cJSON_IsString(query)) return "invalid_query";
        nct_track_t tracks[NCT_MAX_TRACKS];
        size_t count = NCT_MAX_TRACKS;
        esp_err_t ret = music_search(query->valuestring, tracks, &count);
        if (!valid_call(id, epoch)) return "cancelled";
        if (ret != ESP_OK) return "search_failed";
        cJSON_AddStringToObject(result, "status", count ? "found" : "not_found");
        cJSON *list = cJSON_AddArrayToObject(result, "tracks");
        if (!list) return "out_of_memory";
        for (size_t i = 0; i < count; ++i) {
            cJSON *track = cJSON_CreateObject();
            if (!track) return "out_of_memory";
            cJSON_AddItemToArray(list, track);
            cJSON_AddStringToObject(track, "track_id", tracks[i].key);
            cJSON_AddStringToObject(track, "title", tracks[i].title);
            cJSON_AddStringToObject(track, "artist", tracks[i].artist);
            cJSON_AddNumberToObject(track, "duration_seconds", tracks[i].duration);
            cJSON_AddBoolToObject(track, "playable", tracks[i].playable);
        }
        return NULL;
    }
    esp_err_t ret;
    if (!strcmp(name, "play_music")) {
        cJSON *key = cJSON_GetObjectItemCaseSensitive(args, "track_id");
        if (!cJSON_IsString(key)) return "invalid_track_id";
        // Hold music until the model's confirmation turn and voice queue finish.
        speaker_set_voice_pending(true);
        ret = music_play(key->valuestring);
    } else if (!strcmp(name, "control_music")) {
        cJSON *action = cJSON_GetObjectItemCaseSensitive(args, "action");
        if (!cJSON_IsString(action)) return "invalid_action";
        if (!strcmp(action->valuestring, "resume")) speaker_set_voice_pending(true);
        ret = music_control(action->valuestring);
    } else if (!strcmp(name, "set_music_volume")) {
        cJSON *volume = cJSON_GetObjectItemCaseSensitive(args, "percent");
        if (!cJSON_IsNumber(volume) || volume->valueint < 0 || volume->valueint > 100 ||
            volume->valuedouble != volume->valueint) return "invalid_volume";
        ret = music_set_volume(volume->valueint);
    } else if (!strcmp(name, "get_music_status")) ret = ESP_OK;
    else return "unknown_function";
    if (ret != ESP_OK) return ret == ESP_ERR_NOT_FOUND ? "track_not_in_search_results_or_unavailable" : "command_failed";
    cJSON_AddStringToObject(result, "status", "accepted");
    cJSON_AddStringToObject(result, "state", state_name(music_state()));
    cJSON_AddNumberToObject(result, "volume_percent", music_get_volume());
    speaker_music_snapshot_t snapshot;
    speaker_music_snapshot(&snapshot);
    cJSON_AddNumberToObject(result, "position_seconds", snapshot.played_samples / 16000);
    nct_track_t track;
    music_get_track(&track);
    if (track.key[0]) {
        cJSON_AddStringToObject(result, "track_id", track.key);
        cJSON_AddStringToObject(result, "title", track.title);
    }
    return NULL;
}

static void worker(void *arg)
{
    message_t job;
    for (;;) {
        if (xQueueReceive(s_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        if (job.epoch != atomic_load(&s_epoch)) { cJSON_free(job.json); continue; }
        cJSON *call_root = cJSON_Parse(job.json);
        cJSON_free(job.json);
        cJSON *reply = cJSON_CreateObject();
        cJSON *body = reply ? cJSON_AddObjectToObject(reply, "toolResponse") : NULL;
        cJSON *responses = body ? cJSON_AddArrayToObject(body, "functionResponses") : NULL;
        bool failed = !call_root || !responses, hold_voice = false;
        cJSON *call;
        if (responses) cJSON_ArrayForEach(call, cJSON_GetObjectItemCaseSensitive(call_root, "functionCalls")) {
            cJSON *id = cJSON_GetObjectItemCaseSensitive(call, "id");
            cJSON *name = cJSON_GetObjectItemCaseSensitive(call, "name");
            cJSON *args = cJSON_GetObjectItemCaseSensitive(call, "args");
            if (!cJSON_IsString(id) || !id->valuestring[0] || strlen(id->valuestring) > 256 ||
                !cJSON_IsString(name) || strlen(name->valuestring) > 64 || !valid_call(id->valuestring, job.epoch)) continue;
            cJSON *response = cJSON_CreateObject();
            if (!response) { failed = true; break; }
            cJSON_AddItemToArray(responses, response);
            if (!cJSON_AddStringToObject(response, "id", id->valuestring) ||
                !cJSON_AddStringToObject(response, "name", name->valuestring)) { failed = true; break; }
            if (!strcmp(name->valuestring, "set_expression")) cJSON_AddStringToObject(response, "scheduling", "SILENT");
            cJSON *result = cJSON_AddObjectToObject(response, "response");
            if (!result) { failed = true; break; }
            const char *error = result ? execute(name->valuestring, args, result, id->valuestring, job.epoch) : "out_of_memory";
            if (!valid_call(id->valuestring, job.epoch)) {
                cJSON_DeleteItemFromArray(responses, cJSON_GetArraySize(responses) - 1);
                continue;
            }
            if (error && !cJSON_AddStringToObject(result, "error", error)) { failed = true; break; }
            cJSON *action = cJSON_GetObjectItemCaseSensitive(args, "action");
            if (!error && (!strcmp(name->valuestring, "play_music") ||
                (!strcmp(name->valuestring, "control_music") && cJSON_IsString(action) && !strcmp(action->valuestring, "resume"))))
                hold_voice = true;
        }
        cJSON_Delete(call_root);
        if (!failed && responses && cJSON_GetArraySize(responses) && job.epoch == atomic_load(&s_epoch)) {
            message_t message = {.epoch = job.epoch, .json = cJSON_PrintUnformatted(reply), .hold_voice = hold_voice};
            // Bounded wait; the Live task continues draining mic and replies.
            if (!message.json || xQueueSend(s_replies, &message, pdMS_TO_TICKS(1000)) != pdTRUE) {
                cJSON_free(message.json);
                if (job.epoch == atomic_load(&s_epoch)) atomic_store(&s_failed, true);
            }
        }
        if (failed && job.epoch == atomic_load(&s_epoch)) atomic_store(&s_failed, true);
        cJSON_Delete(reply);
    }
}

esp_err_t live_tools_start(void)
{
    if (s_jobs) return ESP_OK;
    s_jobs = xQueueCreate(4, sizeof(message_t));
    s_replies = xQueueCreate(8, sizeof(message_t));
    if (s_jobs && s_replies && xTaskCreate(worker, "live_tools", 8192, NULL, 3, NULL) == pdPASS) return ESP_OK;
    if (s_jobs) vQueueDelete(s_jobs);
    if (s_replies) vQueueDelete(s_replies);
    s_jobs = s_replies = NULL;
    return ESP_ERR_NO_MEM;
}

bool live_tools_add_declarations(cJSON *setup)
{
    cJSON *tools = cJSON_AddArrayToObject(setup, "tools");
    cJSON *group = cJSON_CreateObject();
    if (!tools || !group) { cJSON_Delete(group); return false; }
    cJSON_AddItemToArray(tools, group);
    cJSON *declarations = cJSON_AddArrayToObject(group, "functionDeclarations");
    if (!declarations) return false;
    const char *definitions[] = {
        "{\"name\":\"set_expression\",\"behavior\":\"NON_BLOCKING\",\"description\":\"Silently change the robot face for 15 seconds.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"emotion\":{\"type\":\"STRING\",\"enum\":[\"neutral\",\"happy\",\"sad\",\"surprised\",\"sleepy\"]}},\"required\":[\"emotion\"]}}",
        "{\"name\":\"search_music\",\"description\":\"Search NhacCuaTui by the song-title words actually heard, plus artist if heard. For a music request, try searching even a partial title before asking the user to repeat. Compare returned titles/artists; play only a clearly matching playable track ID, never an unrelated first result.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"query\":{\"type\":\"STRING\"}},\"required\":[\"query\"]}}",
        "{\"name\":\"play_music\",\"description\":\"Queue a playable track ID from the latest music search. Buffering is not proof that playback has started.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"track_id\":{\"type\":\"STRING\"}},\"required\":[\"track_id\"]}}",
        "{\"name\":\"control_music\",\"description\":\"Pause, resume or stop the current song.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"action\":{\"type\":\"STRING\",\"enum\":[\"pause\",\"resume\",\"stop\"]}},\"required\":[\"action\"]}}",
        "{\"name\":\"set_music_volume\",\"description\":\"Set music volume from 0 to 100 percent. Voice volume is unchanged.\",\"parameters\":{\"type\":\"OBJECT\",\"properties\":{\"percent\":{\"type\":\"INTEGER\",\"minimum\":0,\"maximum\":100}},\"required\":[\"percent\"]}}",
        "{\"name\":\"get_music_status\",\"description\":\"Read the actual music state: buffering, playing, paused, idle or error.\"}",
    };
    for (size_t i = 0; i < sizeof(definitions) / sizeof(definitions[0]); ++i) {
        if ((i == 0 && !robot_face_is_available()) || (i != 0 && !music_available())) continue;
        cJSON *definition = cJSON_Parse(definitions[i]);
        if (!definition || !cJSON_AddItemToArray(declarations, definition)) { cJSON_Delete(definition); return false; }
    }
    if (!cJSON_GetArraySize(declarations)) cJSON_DeleteItemFromObjectCaseSensitive(setup, "tools");
    return true;
}
