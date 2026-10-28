#ifndef GEMINI_H
#define GEMINI_H

#include "esp_err.h"
#include <stddef.h>

/**
 * @brief Cấu hình Gemini API Key
 */
#define GEMINI_API_KEY "AIzaSyDN6TqbLtn6N06DJgUGgZkwcaSfqZJfnHk"

 /**
  * @brief Endpoint Google Gemini Interactions API
  */
#define GEMINI_ENDPOINT_URL "https://generativelanguage.googleapis.com/v1beta/interactions?key="

  /**
   * @brief Model Gemini sử dụng
   * Mặc định: "gemini-3.8-flash", "gemini-2.5-flash", "gemini-1.5-flash"
   */
#define GEMINI_MODEL "gemini-3.5-flash-lite"

   /**
    * @brief Gửi prompt tới Google Gemini API và nhận câu trả lời văn bản
    *
    * @param prompt Nội dung văn bản câu hỏi muốn gửi tới Gemini
    * @param response_out Bộ đệm lưu kết quả phản hồi của AI
    * @param max_response_len Kích thước tối đa của bộ đệm response_out
    * @return esp_err_t ESP_OK nếu thành công, hoặc mã lỗi tương ứng
    */
esp_err_t gemini_generate_content(const char* prompt, char* response_out, size_t max_response_len);

#endif // GEMINI_H
