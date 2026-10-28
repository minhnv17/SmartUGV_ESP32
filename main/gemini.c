#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_http_client.h"

#include "gemini.h"

static const char *TAG = "GEMINI";

#define HTTP_RECV_BUFFER_SIZE  8192

/**
 * @brief Google Root CA Certificates (GTS Root R1 & GlobalSign Root CA)
 */
static const char *google_root_ca_pem =
    "-----BEGIN CERTIFICATE-----\n"
    "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\n"
    "bP6MvPJwNQzcmRk13NfIRmPVNnGuV/u3gm3c\n"
    "-----END CERTIFICATE-----\n"
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG\n"
    "A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv\n"
    "b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw\n"
    "MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i\n"
    "YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT\n"
    "aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ\n"
    "jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp\n"
    "xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOX7BQEiIY\n"
    "bv2NYU56Grsi82KbYtxb9wQ1rAlUKIDjf5oui/W6fVJJf01U5guIGb7NUWA8945n\n"
    "+WWGUrtc3qfZdHlEQRtkvnByrhmUoiWREqtWjy5AsiGLIOS5QTELpn8GAShd2MyR\n"
    "U25YaveCoCwoHY2A3L/mGHxU80BxCUSUoePEUrtnNn0107JBEnkwbtUggOmtq64u\n"
    "4gf3CxMC52246E3rAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E\n"
    "BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B\n"
    "AQUFAAOCAQEA1nPnfE9per01gNlDI6aFsPT642U1NRMWZn1+/2104HSKaCPD933m\n"
    "Wy6AZnik4whW61LCRrfEUCj/8RUISUfQ9MQjVCJAQppDxRosTQ28W8binvvREbqP\n"
    "EvcxvdgPt9iKRNU+zPCUwzECj56StTHPFUR/2d6DLSCnmPgt0T55XndnL9nMjML6\n"
    "djxGwwZgg5dddlxwOoAqP/NWhyDnbbcf4FCqda2IV0B9gY6xxzGFF0nmirTiBaDa\n"
    "uVSCdafHTspZl36twu51RVoWhUjKPVWFJnSCYi3WoHKfTr190WXqrNMSBS+Yokyt\n"
    "Tca61PBhnlhm9d630aeJJSmqm5azZm9bBQ==\n"
    "-----END CERTIFICATE-----\n";

typedef struct {
    char *buffer;
    int length;
    int capacity;
} http_response_buffer_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    http_response_buffer_t *resp = (http_response_buffer_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (resp && evt->data && evt->data_len > 0) {
            if (resp->length + evt->data_len < resp->capacity - 1) {
                memcpy(resp->buffer + resp->length, evt->data, evt->data_len);
                resp->length += evt->data_len;
                resp->buffer[resp->length] = '\0';
            } else {
                ESP_LOGW(TAG, "Response buffer gan day, bo qua phan du thua");
            }
        }
    }
    return ESP_OK;
}

/**
 * @brief Escape các ký tự đặc biệt cho JSON string
 */
static void escape_json_string(const char *src, char *dst, size_t max_len) {
    size_t di = 0;
    for (size_t si = 0; src[si] != '\0' && di < max_len - 2; si++) {
        unsigned char c = (unsigned char)src[si];
        if (c == '"') {
            if (di + 2 >= max_len) break;
            dst[di++] = '\\'; dst[di++] = '"';
        } else if (c == '\\') {
            if (di + 2 >= max_len) break;
            dst[di++] = '\\'; dst[di++] = '\\';
        } else if (c == '\n') {
            if (di + 2 >= max_len) break;
            dst[di++] = '\\'; dst[di++] = 'n';
        } else if (c == '\r') {
            if (di + 2 >= max_len) break;
            dst[di++] = '\\'; dst[di++] = 'r';
        } else if (c == '\t') {
            if (di + 2 >= max_len) break;
            dst[di++] = '\\'; dst[di++] = 't';
        } else {
            dst[di++] = (char)c;
        }
    }
    dst[di] = '\0';
}

/**
 * @brief Tìm vị trí giá trị của một field JSON (đảm bảo sau tên field là dấu hai chấm ':')
 */
static const char* find_json_field(const char *json, const char *field) {
    char target[64];
    snprintf(target, sizeof(target), "\"%s\"", field);
    size_t target_len = strlen(target);
    const char *p = json;

    while ((p = strstr(p, target)) != NULL) {
        const char *after = p + target_len;
        while (*after && (*after == ' ' || *after == '\t' || *after == '\r' || *after == '\n')) {
            after++;
        }
        if (*after == ':') {
            return after + 1; // Trỏ ngay sau dấu hai chấm
        }
        p += target_len;
    }
    return NULL;
}

/**
 * @brief Trích xuất nội dung văn bản từ JSON phản hồi của Gemini
 * Hỗ trợ cả Interactions API ("steps" -> "content" -> "text") và generateContent API ("parts" -> "text")
 */
static esp_err_t parse_gemini_text(const char *json_str, char *out_text, size_t max_out_len) {
    if (!json_str || !out_text || max_out_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 1. Kiểm tra nếu có thông báo lỗi từ Google API
    const char *err_key = strstr(json_str, "\"error\"");
    if (err_key) {
        const char *val_start = find_json_field(err_key, "message");
        if (val_start) {
            while (*val_start && isspace((unsigned char)*val_start)) val_start++;
            if (*val_start == '"') val_start++;
            snprintf(out_text, max_out_len, "Lỗi từ Gemini API: ");
            size_t idx = strlen(out_text);
            while (*val_start && *val_start != '"' && idx < max_out_len - 1) {
                out_text[idx++] = *val_start++;
            }
            out_text[idx] = '\0';
            return ESP_FAIL;
        }
        snprintf(out_text, max_out_len, "Gemini API bao loi khong xac dinh");
        return ESP_FAIL;
    }

    // 2. Định vị vị trí bắt đầu tìm trường "text"
    // Bắt đầu từ mảng "steps" (Interactions API) hoặc "candidates" (generateContent API)
    const char *search_start = strstr(json_str, "\"steps\"");
    if (!search_start) {
        search_start = strstr(json_str, "\"candidates\"");
    }
    if (!search_start) {
        search_start = json_str;
    }

    // 3. Tìm trường "text" (được theo sau bởi dấu ':')
    const char *val_start = find_json_field(search_start, "text");
    if (!val_start) {
        ESP_LOGE(TAG, "Khong tim thay truong 'text' trong JSON phan hoi. Raw JSON: %s", json_str);
        return ESP_ERR_NOT_FOUND;
    }

    while (*val_start && isspace((unsigned char)*val_start)) {
        val_start++;
    }

    if (*val_start != '"') {
        return ESP_ERR_INVALID_RESPONSE;
    }
    val_start++; // Bỏ qua dấu nháy kép mở đầu

    // 4. Giải mã văn bản, unescape ký tự đặc biệt và Unicode \uXXXX
    size_t out_idx = 0;
    const char *p = val_start;
    while (*p && out_idx < max_out_len - 1) {
        if (*p == '"') {
            break; // Kết thúc chuỗi text
        }
        if (*p == '\\') {
            p++;
            if (!*p) break;
            if (*p == 'n') {
                out_text[out_idx++] = '\n';
            } else if (*p == 'r') {
                out_text[out_idx++] = '\r';
            } else if (*p == 't') {
                out_text[out_idx++] = '\t';
            } else if (*p == '"') {
                out_text[out_idx++] = '"';
            } else if (*p == '\\') {
                out_text[out_idx++] = '\\';
            } else if (*p == '/') {
                out_text[out_idx++] = '/';
            } else if (*p == 'u') {
                // Giải mã ký tự unicode dạng \uXXXX
                if (isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2]) &&
                    isxdigit((unsigned char)p[3]) && isxdigit((unsigned char)p[4])) {
                    char hex[5] = {p[1], p[2], p[3], p[4], '\0'};
                    uint32_t cp = (uint32_t)strtoul(hex, NULL, 16);
                    p += 4;
                    if (cp <= 0x7F) {
                        out_text[out_idx++] = (char)cp;
                    } else if (cp <= 0x7FF) {
                        if (out_idx + 2 < max_out_len) {
                            out_text[out_idx++] = (char)(0xC0 | ((cp >> 6) & 0x1F));
                            out_text[out_idx++] = (char)(0x80 | (cp & 0x3F));
                        }
                    } else {
                        if (out_idx + 3 < max_out_len) {
                            out_text[out_idx++] = (char)(0xE0 | ((cp >> 12) & 0x0F));
                            out_text[out_idx++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                            out_text[out_idx++] = (char)(0x80 | (cp & 0x3F));
                        }
                    }
                }
            } else {
                out_text[out_idx++] = *p;
            }
        } else {
            out_text[out_idx++] = *p;
        }
        p++;
    }
    out_text[out_idx] = '\0';
    return ESP_OK;
}

esp_err_t gemini_generate_content(const char *prompt, char *response_out, size_t max_response_len) {
    if (!prompt || !response_out || max_response_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strcmp(GEMINI_API_KEY, "YOUR_GEMINI_API_KEY_HERE") == 0 || strlen(GEMINI_API_KEY) == 0) {
        ESP_LOGE(TAG, "Vui long dien GEMINI_API_KEY vao file main/gemini.h truoc khi chay!");
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Tạo URL request cho Google Gemini Interactions API
    char url[384];
    snprintf(url, sizeof(url), "%s%s", GEMINI_ENDPOINT_URL, GEMINI_API_KEY);

    // 2. Escape prompt và đóng gói payload JSON chuẩn cho Interactions API
    char escaped_prompt[1024];
    escape_json_string(prompt, escaped_prompt, sizeof(escaped_prompt));

    char post_payload[1536];
    snprintf(post_payload, sizeof(post_payload),
             "{\"model\":\"%s\",\"input\":\"%s\"}",
             GEMINI_MODEL, escaped_prompt);

    // 3. Cấp phát buffer chứa dữ liệu phản hồi
    char *recv_buf = (char *)malloc(HTTP_RECV_BUFFER_SIZE);
    if (!recv_buf) {
        ESP_LOGE(TAG, "Khong du bo nho cap phat HTTP receive buffer!");
        return ESP_ERR_NO_MEM;
    }
    recv_buf[0] = '\0';

    http_response_buffer_t resp_data = {
        .buffer = recv_buf,
        .length = 0,
        .capacity = HTTP_RECV_BUFFER_SIZE
    };

    // 4. Cấu hình HTTPS client (timeout 45 giây cho xử lý AI)
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 45000,
        .cert_pem = google_root_ca_pem,
        .skip_cert_common_name_check = true,
        .event_handler = http_event_handler,
        .user_data = &resp_data,
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Khong the khoi tao esp_http_client");
        free(recv_buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-goog-api-key", GEMINI_API_KEY);
    esp_http_client_set_post_field(client, post_payload, strlen(post_payload));

    ESP_LOGI(TAG, "Dang gui HTTPS request toi Gemini Interactions API (Model: %s)...", GEMINI_MODEL);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "HTTPS Status = %d, Content Length = %lld",
                 status_code, esp_http_client_get_content_length(client));

        if (status_code == 200) {
            err = parse_gemini_text(recv_buf, response_out, max_response_len);
        } else {
            ESP_LOGE(TAG, "Server tra ve ma loi: %d", status_code);
            parse_gemini_text(recv_buf, response_out, max_response_len);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGE(TAG, "HTTPS request that bai: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    free(recv_buf);
    return err;
}
