# SmartUGV ESP32 — Gemini Live

Firmware ESP-IDF v6.1 cho LCDWIKI ES3C28P (ESP32-S3 N16R8).
`main.c` hiện khởi chạy Gemini Live qua WebSocket bảo mật.

## System prompt NOVA

Nội dung duy nhất nằm trong `main/system_promt.txt` (UTF-8). CMake nhúng file
bằng `EMBED_TXTFILES`; `gemini.c` đưa nguyên văn vào `systemInstruction` khi
mở/khôi phục phiên Live. Sửa file rồi build và nạp lại để áp dụng; không cần
chép prompt vào chuỗi C. Khi thêm công cụ phần cứng, cập nhật phần mô tả khả
năng hiện tại trong prompt cùng với khai báo công cụ của ứng dụng.

NOVA xưng “em”, gọi người dùng là “chủ”, mặc định trả lời 1–2 câu. Cách xưng
hô này không xác minh danh tính. Chưa có nhận diện chủ nhân, bộ nhớ lâu dài
hay từ đánh thức. Prompt có quy tắc bỏ qua hội thoại nền nhưng không thay
thế bộ nhận từ đánh thức hoặc cơ chế kiểm tra quyền ở firmware.

Chế độ chờ local → phát hiện từ đánh thức → mở WebSocket → hội thoại → hết
phiên là hướng triển khai tiếp theo. Firmware hiện vẫn mở Live tự động và
gửi mic liên tục khi sẵn sàng (gửi im lặng trong lúc loa phát). Việc thay
prompt không tự thay đổi kết nối hoặc cách gửi mic.

## Sử dụng

1. Wi-Fi nằm trong `main/wifi.h`. Với API key, tạo file cấu hình cục bộ:

   ```powershell
   Copy-Item main/secrets.example.h main/secrets.h
   ```

   Điền key thật vào `main/secrets.h`. File này đã được `.gitignore` bỏ qua;
   chỉ commit `secrets.example.h` có giá trị mẫu. Nếu đã có `secrets.h`, sửa
   file hiện có, không ghi đè bằng file mẫu.
   Live dùng `GEMINI_MODEL` trong cùng file `main/gemini.h`.
2. Mở môi trường ESP-IDF, chạy `idf.py build`, sau đó
   `idf.py -p COM17 flash monitor`.
3. Chờ `LIVE SAN SANG`, nói gần mic rồi dừng một nhịp.
   **Không cần bấm BOOT.** Gemini tự nhận biết lượt nói.
4. Xem `Ban noi:` và `Gemini:` ở 115200 baud, UTF-8.
   Mỗi dòng có thể chỉ là một phần transcript đang được truyền về.
5. Câu trả lời được phát qua loa. Chờ loa nói xong rồi nói tiếp; bản hiện tại
   tạm gửi im lặng từ mic khi loa đang phát để tránh tự thu lại tiếng loa.

Khi Live sẵn sàng, mic gửi âm thanh liên tục tới Google Gemini, bao gồm
âm thanh nền. Ngắt nguồn để dừng phiên. Lịch sử nằm trong phiên Live;
reset board sẽ mở phiên mới. Không chia sẻ API key hoặc session handle.

## Luồng âm thanh

- LMA2718B → ES8311 ADC → I2S → PCM mono LE16, 16 kHz.
- Tăng biên độ mic 4 lần, bão hòa trong miền int16 để tránh tràn số.
- Mỗi gói khoảng 20 ms, mã hóa base64 và gửi `realtimeInput.audio`.
- Gemini dùng VAD phía server, khoảng im lặng 600 ms để kết thúc lượt nói.
- Nhận transcript đầu vào, transcript câu trả lời và audio PCM 24 kHz.
- Audio trả về được sao chép vào hàng đợi PSRAM khoảng 10 giây, không chặn
  task WebSocket. Task loa lọc FIR rồi chuyển mẫu 24 → 16 kHz, giữ đúng tốc độ.
- ES8311 ADC và DAC dùng chung clock I2S 16 kHz; phát qua GPIO8 và ampli GPIO1.
- Bật ampli ngay khi tích đệm, giữ ít nhất 200 ms ổn định trước mẫu giọng nói
  đầu tiên để không cắt nửa đầu từ. Thời gian này trùng với chờ đệm khi có thể.
- Bộ đệm đợi đủ lượng mẫu PCM thực tế, ban đầu 800 ms, thích nghi trong
  khoảng 400–1600 ms khi thiếu audio mạng; câu trả lời ngắn đã nhận đủ sẽ phát sớm; TX tự phát im
  lặng khi hết dữ liệu để tránh lặp lại DMA cũ.
- Âm lượng DAC: `SPEAKER_DAC_VOLUME` trong `main/mic.h`, hiện `0xBF` = 0 dB.
  `0xBF` = 0 dB; mỗi bước tương ứng 0,5 dB.

Đây là hội thoại luân phiên, chưa có acoustic echo cancellation: mic vẫn đọc
nhưng gửi im lặng trong lượt trả lời và khi hàng đợi loa chưa phát xong, cộng
600 ms đuôi âm. Không ngắt lời bằng giọng nói trong lúc loa đang phát.
Khi server báo ngắt lời hoặc mất kết nối, tăng epoch để bỏ các frame cũ và
ngắt ampli. Các tình huống mất mạng dài vẫn cần kiểm thử thêm.

## Kết nối và bộ nhớ

- Xác thực API key qua header, không đưa key vào URL hay log.
- TLS certificate bundle, xác minh chứng chỉ cross-signed và tên máy chủ.
- Bộ đệm nhận JSON 128 KB và audio 128 KB trong PSRAM; xử lý cả các phần
  DATA nhỏ hơn frame và continuation frames, giới hạn kích thước tin nhắn.
- Bật `ESP_WS_CLIENT_SEPARATE_TX_LOCK` để tránh khóa nhận chặn luồng gửi.
- Chỉ gửi mic sau `setupComplete`; âm thanh khi mất kết nối bị bỏ qua.
- Khi mất kết nối hoặc nhận `goAway`, thử nối lại với backoff tối đa 30 giây.
  Dùng session resumption handle khi server cấp để giữ ngữ cảnh. Nếu không có
  handle hoặc handle bị từ chối, mở phiên mới; log báo trạng thái resume.
- Tắt Wi-Fi modem sleep (`WIFI_PS_NONE`) để ưu tiên độ trễ truyền âm thanh.
- Bật context window compression. Khôi phục phiên có thể bỏ lỡ âm thanh
  đang nói lúc mạng bị gián đoạn; các tình huống mất mạng dài chưa được kiểm thử.

## Hardware

| Chức năng | GPIO |
| --- | --- |
| ES8311 SDA / SCL, địa chỉ 0x18 | 16 / 15 |
| I2S MCLK / BCLK / LRCK | 4 / 5 / 7 |
| I2S mic vào ESP32 | 6 |
| I2S phát ra ES8311 DAC | 8 |
| Bật ampli loa mức thấp, tự tắt khi hết âm thanh | 1 |

Cấu hình N16R8: flash 16 MB, PSRAM Octal 40 MHz; phân vùng app 1500 KB.
Dependency cJSON và esp_websocket_client được quản lý trong
`main/idf_component.yml`.

## Mã nguồn

- `main.c`: khởi tạo mic, Wi-Fi và gọi `gemini_start()`.
- `gemini.c/.h`: toàn bộ Gemini Live: cấu hình, WebSocket, PCM, transcript,
  session resumption và hook audio/interruption. Model và API key ở `gemini.h`.
- `mic.c/.h`: codec ES8311, bus I2C, I2S RX/TX và mức âm lượng DAC.
- `speaker.c/.h`: hàng đợi PSRAM, chuyển mẫu và task phát loa.
- `wifi.c/.h`: Wi-Fi station.

Các bản thử nghiệm văn bản và ghi WAV gửi REST đã được loại bỏ. Dự án chỉ
còn một luồng Gemini Live và một nơi cấu hình Gemini.

## Chẩn đoán

- Chưa có `LIVE SAN SANG`: xem log Wi-Fi, TLS, WebSocket và setup timeout.
- Không có transcript: nói gần mic, kiểm tra mạng và quyền truy cập model Live.
- `Dang phat Gemini qua loa` và `Phat xong ... mau PCM` ghi lại đường phát I2S.
  Nghe thử loa để xác nhận tiếng thực tế; log TX không tự xác minh chất lượng âm.
- Tiếng nhỏ/rè: chỉnh `SPEAKER_DAC_VOLUME` và kiểm tra loa/nguồn cấp.
- Thông báo TX chuyển master sang slave là bình thường: TX dùng clock chung với RX.
- Lỗi khóa ws-client: kiểm tra `CONFIG_ESP_WS_CLIENT_SEPARATE_TX_LOCK=y`.
- Chỉ mở một serial monitor tại một thời điểm. Khi mở cổng, dùng trình tự
  reset chuẩn của `idf.py monitor` để tránh vô tình đưa board vào reset/BOOT.

## Kiểm thử ngày 01/11/2026

- Build thành công, nạp COM17 và xác minh flash; PSRAM 8 MB hoạt động.
- Nhận `setupComplete` từ `gemini-3.8-live` trên board qua WSS và TLS.
- Sau khi bật separate TX lock, nhận trọn câu trả lời và 276000 byte PCM
  trong một lượt; phép tính “12 × 2” được trả lời “24”.
- Bản tăng mic 4 lần và VAD 1000 ms chạy hai lượt liên tiếp, nhận lần lượt
  290882 và 216002 byte PCM, transcript phản hồi đầy đủ, không ngắt phiên
  trong hai lượt thử. Tên “Minh” bị nhận thành “mình”, nên chưa xác nhận
  được bài kiểm thử nhớ đúng tên. Cần tiếp tục tinh chỉnh độ chính xác giọng nói.
- Đã quan sát tự kết nối lại khi lỗi mạng/khóa; có lần resume bị server
  từ chối (1008), client xóa handle và mở phiên mới. Chưa kiểm thử thành công
  việc giữ nguyên ngữ cảnh qua một lần resume hoặc một phiên kéo dài.
- Các kết quả trên được ghi trước khi thêm loa; chưa kiểm thử AEC.

Tài liệu: [LCDWIKI](https://www.lcdwiki.com/2.8inch_ESP32-S3_Display),
[Live WebSocket](https://ai.google.dev/gemini-api/docs/live-api/get-started-websocket),
[Session management](https://ai.google.dev/gemini-api/docs/live-api/session-management).

## Kiểm thử phát loa ngày 01/11/2026

- Build/nạp thành công; đọc lại DAC unmute `0x09=0x0C`, nguồn DAC
  `0x12=0x00`, âm lượng `0x32=0xBF`.
- Âm thử 1 kHz, 1 giây được tạo sẵn trên ESP32 phát liền mạch; người dùng
  xác nhận không hụt. Mã phát âm thử tự động đã được gỡ sau chẩn đoán.
- Quan sát khoảng chờ audio mạng tới 380 ms, vượt bộ đệm DMA khoảng 90 ms.
  Đổi từ chờ thời gian cố định sang tích đủ mẫu PCM, có phát hiện starvation.
- Gemini trả lời qua mic/loa nhiều lượt, gồm “2 + 3 = 5” và câu nối tiếp
  “kết quả đó nhân 5 = 25”. Số mẫu phát khớp tỷ lệ chuyển 24 → 16 kHz.
- Sau khi bật ampli sớm, người dùng xác nhận đầu câu đã phát đủ và nghe ổn.
- VAD 600 ms; đệm PCM ban đầu 800 ms, thích nghi 400–1600 ms; Wi-Fi không
  modem sleep. Độ trễ tổng còn phụ thuộc mạng và model; chưa đo thống kê dài hạn.

## Kiểm thử prompt NOVA ngày 01/11/2026

- Nhúng `system_promt.txt` UTF-8 qua CMake, build/nạp thành công.
- Board ghi nhận prompt nhúng và nhận `setupComplete` từ Gemini Live.
- Kiểm tra API với nguyên nội dung file: AI tự giới thiệu NOVA, xưng em/chủ,
  nói rõ chưa có công cụ điều khiển động cơ và chưa lưu tên lâu dài.
- Chưa triển khai WakeNet/chờ đánh thức hoặc xác minh chủ nhân; kết nối và
  cách gửi mic vẫn như trước. Quy tắc prompt không bảo đảm lọc hết tiếng TV.

## API key và Git

`main/secrets.h` chỉ tồn tại trên máy phát triển, được include riêng trong
`gemini.c`. Kiểm tra quy tắc bỏ qua bằng `git check-ignore -v main/secrets.h`.
`main/gemini.h` chỉ còn model và API công khai, không chứa key thật.

`.gitignore` không xóa bí mật đã commit trong lịch sử. Repo có commit cũ
chứa key; nếu commit đó từng được chia sẻ, thu hồi key cũ và tạo key thay thế
trong Google AI Studio. Tách file không tự thu hồi key hoặc xóa lịch sử Git.
Key hiện vẫn được biên dịch vào firmware; không chia sẻ firmware chứa key thật.
