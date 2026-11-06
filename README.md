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
3. Chờ `LIVE SAN SANG`/`DANG NGHE`, nói rồi dừng khoảng một giây.
   **Không cần bấm nút.** Gemini tự nhận biết lượt nói.
4. Xem `Ban noi:` và `Gemini:` ở 115200 baud, UTF-8.
   Mỗi dòng có thể chỉ là một phần transcript đang được truyền về.
5. Câu trả lời được phát qua loa. Chờ loa nói xong rồi nói tiếp; bản hiện tại
   tạm gửi im lặng từ mic khi loa đang phát để tránh tự thu lại tiếng loa.

Khi Live sẵn sàng, mic gửi âm thanh liên tục tới Google Gemini, bao gồm
âm thanh nền. Ngắt nguồn để dừng phiên. Lịch sử nằm trong phiên Live;
reset board sẽ mở phiên mới. Không chia sẻ API key hoặc session handle.

## Màn hình trạng thái và biểu cảm

Màn hình tích hợp ILI9341V dùng bố cục ngang 320×240 để đặt trên xe,
hiển thị khuôn mặt Cá, có chớp mắt và
chuyển động miệng khi đang trả lời. Dòng trạng thái đổi theo firmware: khởi
động, kết nối Wi-Fi/Gemini, đang nghe, đang xử lý, đang trả lời, kết nối lại
và lỗi. `DANG NGHE` nghĩa là Live sẵn sàng gửi mic, chưa phải nhận từ đánh
thức. `DANG XU LY` là chỉ báo từ transcript mới nhận, tối đa 10 giây nếu
không có phản hồi; chuyển động miệng là hoạt ảnh, chưa bám theo biên độ PCM.
Trạng thái trả lời bao gồm tích đệm và phần đuôi âm của loa.

Gemini được cấp công cụ `set_expression` với 5 biểu cảm: `neutral`, `happy`,
`sad`, `surprised`, `sleepy`. Prompt hướng dẫn chọn theo sắc thái hội thoại
hoặc yêu cầu trực tiếp, ví dụ “Cá ơi, làm mặt vui”, “Cá ơi, làm mặt buồn”,
“Cá ơi, làm mặt ngạc nhiên”. Việc gọi công cụ do model quyết định; không
phải bộ nhận diện cảm xúc của người dùng. Mỗi biểu cảm giữ tối đa 15 giây
kể từ lệnh gần nhất rồi về bình thường. Lỗi/kết nối lại được ưu tiên mặt
buồn. Nhãn trên LCD dùng tiếng Việt không dấu với font 5×7 tích hợp.

Task màn hình ưu tiên thấp, không vẽ trong callback WebSocket. Framebuffer
150 KB ở PSRAM, truyền DMA từng dải 16 dòng qua bộ đệm SRAM 10 KB; không
cần thêm LVGL hoặc thư viện ngoài. Phản hồi công cụ được xếp hàng và gửi
bởi task Live với `scheduling: SILENT`, tránh thao tác socket trong callback.
Nếu khởi tạo LCD thất bại, hội thoại vẫn chạy và không khai báo công cụ màn
hình; lỗi truyền LCD sau đó tắt đèn nền và báo công cụ không khả dụng.
Phản hồi `accepted` chỉ xác nhận firmware nhận lệnh, chưa xác minh ảnh trên
LCD vật lý. Cảm ứng FT6336G được dùng cho các nút điều khiển nhạc.

Mặc định màn hình xoay 90°. Nếu lắp ngang theo chiều ngược lại và hình bị
lộn đầu, chạy `idf.py menuconfig` → `Robot Display` → bật
`Flip landscape display 180 degrees`, rồi build/nạp lại. Hai chiều đều dùng
bố cục 320×240; khuôn mặt và chữ được vẽ lại theo khổ ngang.

Sau khi build/nạp, kiểm tra khuôn mặt xuất hiện trước khi Wi-Fi sẵn sàng,
trạng thái nghe/nói qua vài lượt hội thoại, rồi lần lượt yêu cầu các biểu
cảm. Kiểm tra tự về bình thường sau 15 giây và thử mất mạng/kết nối lại.
Quan sát LCD và nghe loa cùng lúc để xác nhận hoạt ảnh không làm hụt audio.

## Nhạc trực tuyến NhacCuaTui

ESP tự gọi API tìm kiếm và nhận luồng MP3 từ NhacCuaTui; không cần thẻ SD
hoặc backend trên máy tính. Cần Wi-Fi hoạt động. Các lệnh mẫu:

- “Cá ơi, mở bài Như Một Người Rưng của OSAD.”
- “Cá ơi, tìm bài Nơi Này Có Anh của Sơn Tùng.”
- Sau khi tạm dừng: “Tiếp tục phát nhạc”, “Dừng nhạc”, “Mở bài khác”.
- “Đặt âm lượng nhạc 50 phần trăm.” Âm lượng giọng Gemini giữ nguyên.

Gemini gọi `search_music(query)` để lấy tối đa 5 kết quả, rồi
`play_music(track_id)` bằng mã bài từ lần tìm gần nhất. Khi không rõ bản
nào, model được hướng dẫn hỏi lại. `control_music` hỗ trợ pause/resume/stop;
`set_music_volume` nhận 0–100, mặc định 70%; `get_music_status` đọc trạng
thái. Lệnh phát trả `buffering` khi đã nhận yêu cầu; chỉ trạng thái `playing`
mới cho biết bộ phát đã bắt đầu ghi PCM vào I2S, chưa xác minh tiếng vật lý.

**Trong lúc nhạc phát, chạm nút `TAM DUNG` ở dưới màn hình; chờ khoảng một
giây rồi nói lệnh tiếp.** Nút bên trái đổi thành `TIEP TUC` để phát tiếp;
nút `DUNG` bên phải kết thúc bài. Các nút cũng xuất hiện khi đang tải nhạc.
Khi phát/tạm dừng, màn hình chuyển sang giao diện nghe nhạc riêng: tên bài
dài tự cuộn, ca sĩ, cột biên độ âm thanh, thời gian và thanh tiến độ. Cột
âm thanh lấy peak của PCM nhạc đã chỉnh gain, không phải phân tích phổ FFT;
khi tạm dừng hoặc nhạc không có PCM mới thì cột về mức nghỉ. Thời gian dựa
trên số mẫu đã gửi I2S nên có thể đi trước tiếng thực tế khoảng độ sâu DMA.
Nút `-`/`+` giảm/tăng 5%; chạm hoặc kéo thanh âm lượng rồi thả để đặt 0–100%.
Gain đổi dần qua một gói PCM để giảm tiếng tách khi thay âm lượng.
BOOT không được dùng để điều khiển nhạc. Hiện chưa có khử vọng AEC; mic gửi
im lặng trong lúc loa phát nhạc, nên cần dùng cảm ứng để tạm dừng trước khi
nói yêu cầu đổi bài hoặc trò chuyện.

Cảm ứng FT6336G địa chỉ I2C `0x38`, SDA16/SCL15 dùng chung bus I2C0 đã được
mic khởi tạo; chỉ reset cảm ứng qua GPIO18. Driver đọc theo chu kỳ 20 ms,
không sử dụng ngắt GPIO17 và không tạo bus I2C thứ hai. Tọa độ tự xoay theo
`Flip landscape display 180 degrees`. Nút chỉ nhận chạm khi đã xuất hiện
trong một frame LCD truyền xong; cần chạm/thả ổn định, giữ ngón không lặp
lệnh, kéo ra ngoài nút hoặc chạm nhiều ngón sẽ hủy lần chạm. Riêng kéo thanh
âm lượng giữ thao tác trong vùng âm lượng, kẹp giá trị ở 0 và 100%. Khi cảm ứng báo
lỗi, nút chuyển màu xám và ngừng nhận thao tác đến khi đọc cảm ứng ổn định
trở lại. Cần thử độ khớp tọa độ trên màn hình thật sau khi nạp.

Tìm kiếm/lấy thông tin chạy ở task công cụ, ngoài callback WebSocket. Nhạc
chạy ở task riêng; duy nhất task loa ghi I2S. Hàng đợi giọng được ưu tiên,
nhạc giữ lại dữ liệu khi giọng đang phát. Reset phiên Gemini hủy các công
cụ đang đợi của phiên cũ; một bài đang phát tiếp tục độc lập với phiên đó.

Nguồn được chọn là MP3 128 kbps có `status: 1`, `onlyVIP: false` và bài có
`statusPlay: 1`. Mỗi lần mở bài lấy URL mới từ chi tiết bài. Metadata ID3 lớn
được đọc bỏ từng phần; MP3 stereo được trộn mono và đổi tần số có lọc xuống
16 kHz để dùng chung clock với mic. Bộ đệm PCM nhạc tối đa khoảng 1,9 giây
ở PSRAM; không tải toàn bộ bài vào RAM. Dải tần nhạc bị giới hạn bởi đầu ra
mono 16 kHz hiện tại.

PCM sau chuyển mẫu được gom thành gói 480 mẫu (30 ms); phần cuối lẻ vẫn
được gửi khi hết bài. Khi có nhạc sẵn sàng, task loa kiểm tra giọng theo
kiểu không chờ rồi ghi I2S để DMA điều tiết tốc độ. Bản trước chờ hàng đợi
giọng 20 ms mỗi gói, có thể làm hụt DMA nếu gói nhạc ngắn hơn 20 ms. Khi chờ
tích đệm vẫn nhường CPU cho decoder. Log `Music PCM refill gap` giúp theo
dõi khoảng chờ dài trong lúc phát liên tục.

Tạm dừng giữ bộ đệm/decoder, đóng kết nối HTTP. Tiếp tục mở kết nối Range
tại byte kế tiếp; nếu CDN không hỗ trợ đúng Range hoặc link không còn hiệu
lực, báo lỗi và cần mở lại bài. Đổi/dừng bài tăng epoch để bỏ PCM cũ. Mạng
quá chậm, lỗi TLS, dữ liệu lỗi hoặc bài không khả dụng được báo ở log và
màn hình. Tên bài trên LCD được bỏ dấu, viết hoa, tối đa 51 ký tự.

Thư viện `esp_audio_codec` 2.6.2 và `esp_audio_effects` 1.4.4 được quản lý
trong manifest. Nguồn khảo sát và các endpoint nằm ở
[báo cáo NhacCuaTui](docs/nhaccuatui_research.md).

Kiểm thử host độc lập với phần cứng sau khi đã tải dependencies bằng IDF:

```powershell
cmake -S tests -B build/host_music_tests -G "Visual Studio 17 2022" -A x64
cmake --build build/host_music_tests --config Debug
ctest --test-dir build/host_music_tests -C Debug --output-on-failure
```

Các bài kiểm thử bao gồm parser dữ liệu/cờ khả dụng/URL, giữ nguyên biên
UTF-8, nhãn tiếng Việt và mô phỏng task loa: ưu tiên giọng, giữ nhạc khi tạm
dừng, bỏ frame bài cũ khi đổi bài, tiếp tục sau khi flush giọng, phát đoạn
ngắn. Mô phỏng chạy đơn luồng, chưa thay cho kiểm tra timing trên ESP.

## Luồng âm thanh

- LMA2718B → ES8311 ADC → I2S → PCM mono LE16, 16 kHz.
- Lọc DC/ù trầm khoảng 51 Hz, gain mic thích nghi tối đa 12 lần thay cho
  cố định 4 lần. Gain tăng dần khi tín hiệu yếu vượt ngưỡng thích nghi,
  giảm khi tín hiệu lớn và giới hạn peak đầu ra 30000 để tránh cắt đỉnh.
  Gần im lặng không đẩy gain lên tối đa; không dùng noise gate cắt âm nhỏ.
- Mỗi gói khoảng 20 ms, mã hóa base64 và gửi `realtimeInput.audio`.
- Gemini dùng VAD phía server: start HIGH, end LOW, prefix 80 ms và khoảng
  im lặng 1000 ms để giữ tên bài dài/ngập ngừng trong cùng lượt. Đổi từ
  600 lên 1000 ms làm thời gian chờ trả lời tăng khoảng 400 ms.
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
| LCD SPI MOSI / SCLK / CS / DC | 11 / 12 / 10 / 46 |
| LCD đèn nền, bật mức cao | 45 |
| FT6336G SDA / SCL, địa chỉ 0x38, chung bus codec | 16 / 15 |
| FT6336G reset, mức thấp | 18 |
| ES8311 SDA / SCL, địa chỉ 0x18 | 16 / 15 |
| I2S MCLK / BCLK / LRCK | 4 / 5 / 7 |
| I2S mic vào ESP32 | 6 |
| I2S phát ra ES8311 DAC | 8 |
| Bật ampli loa mức thấp, tự tắt khi hết âm thanh | 1 |

Cấu hình N16R8: flash 16 MB, PSRAM Octal 40 MHz; phân vùng app 1500 KB.
Dependency cJSON và esp_websocket_client được quản lý trong
`main/idf_component.yml`.
LCD dùng `esp_lcd`/SPI có sẵn trong ESP-IDF, reset mềm vì chân reset màn
hình nối chung EN của ESP32. Cấu hình panel theo
[mã khởi tạo ILI9341V của LCDWIKI](https://www.lcdwiki.com/res/ES3C28P/ILI9341V_Init.txt).

## Mã nguồn

- `main.c`: khởi tạo mic, Wi-Fi và gọi `gemini_start()`.
- `gemini.c/.h`: toàn bộ Gemini Live: cấu hình, WebSocket, PCM, transcript,
  session resumption và hook audio/interruption. Model và API key ở `gemini.h`.
- `mic.c/.h`: codec ES8311, bus I2C, I2S RX/TX và mức âm lượng DAC.
- `mic_frontend.c/.h`: lọc DC, gain mic thích nghi, giới hạn peak và thống kê mức thu.
- `speaker.c/.h`: hàng đợi PSRAM, chuyển mẫu và task phát loa.
- `wifi.c/.h`: Wi-Fi station.
- `robot_face.c/.h`: LCD ILI9341, khuôn mặt động, trạng thái và biểu cảm.
- `nct_catalog.c`, `nct_music.c/.h`: parser và HTTP cho tìm bài/chi tiết NCT.
- `music.c/.h`: tải MP3, giải mã, chuyển mẫu, tạm dừng/tiếp tục.
- `live_tools.c/.h`: khai báo công cụ, worker HTTP và phản hồi theo phiên Live.
- `display_text.c/.h`: chuyển tên bài tiếng Việt thành nhãn ASCII trên LCD.
- `touch_input.c/.h`, `touch_ui.c/.h`: FT6336G, xoay tọa độ và nút nhạc cảm ứng.

Các bản thử nghiệm văn bản và ghi WAV gửi REST đã được loại bỏ. Hội thoại
dùng một luồng Gemini Live và một nơi cấu hình Gemini; nhạc là nguồn âm
thanh riêng, dùng chung task phát loa.

## Chẩn đoán

- Chưa có `LIVE SAN SANG`: xem log Wi-Fi, TLS, WebSocket và setup timeout.
- Không có transcript: kiểm tra trạng thái `DANG NGHE`, log `MIC level`,
  mạng và quyền truy cập model Live. Mic gửi im lặng khi loa còn phát.
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

## Kiểm thử màn hình ngày 06/11/2026

- Build thành công với ESP-IDF 6.1 cho ESP32-S3; app còn khoảng 29% phân vùng.
- Render thử bố cục ngang 320×240 bằng chính các hàm vẽ C: đủ 5 biểu cảm và mặt lỗi, chữ nằm trong
  màn hình. Đã chạy 240 tổ hợp trạng thái/biểu cảm/khung hoạt ảnh với vùng
  canh hai đầu framebuffer không bị ghi đè.
- Chưa nạp bản màn hình lên board, chưa xác nhận màu/hướng ảnh trên LCD thật
  hoặc việc Gemini gọi công cụ trong phiên Live thực tế. Cần kiểm tra cùng
  mic/loa theo hướng dẫn ở phần màn hình.

## Kiểm thử tích hợp nhạc ngày 06/11/2026

- Build ESP-IDF 6.1 cho ESP32-S3 thành công, app còn khoảng 20% phân vùng.
- Chín bài kiểm thử host đã qua; render 1170 tổ hợp trạng thái/biểu cảm/khung
  hoạt ảnh/âm lượng/tiến độ, kiểm tra vùng canh framebuffer và bố cục nhạc 320×240.
- API/đoạn MP3 được kiểm tra bằng HTTP trên máy trong bước khảo sát.
- Chưa nạp bản nhạc lên board, chưa kiểm tra phiên Gemini gọi các công cụ
  mới, TLS đồng thời trên ESP, giải mã/phát nguyên bài, cảm ứng và Range
  tiếp tục trên phần cứng. Cần thử hội thoại → tìm/phát → chạm tạm dừng → tiếp tục/đổi
  bài → dừng, nghe loa và xem LCD; thử thêm mất mạng và bài lỗi.
- Đã bỏ task/nút BOOT điều khiển nhạc. Các kiểm thử cảm ứng bao gồm hai
  chiều lắp ngang, mép nút, chạm/thả, giữ ngón, kéo ngón, đổi giao diện giữa
  lần chạm, nhiều ngón và lỗi đọc cảm ứng. Chưa xác nhận tọa độ vật lý.
- Kiểm thử DMA mô phỏng với 60 gói PCM 10 ms tái hiện 59 lần hụt ở mã cũ,
  không còn hụt sau sửa. Kiểm tra gain đổi êm, đếm mẫu tiến độ và volume slider
  đều qua. Đọc log COM17 trong 12 giây không có dòng phát nhạc phù hợp;
  chưa nghe lại trên loa thật để xác nhận tình trạng rè đã hết.

## Thu giọng nói và tên bài hát ngày 06/11/2026

- Build ESP-IDF 6.1 thành công, app còn 20% phân vùng. Cả 10 bài kiểm thử
  host qua; kiểm tra offline chuỗi JSON setup và sáu khai báo công cụ qua.
  Gemini Live trả `setupComplete` với cấu hình VAD, prompt và công cụ mới
  trong hai phiên thử trên máy. Câu văn bản có tên bài ngập ngừng gọi
  `search_music`; câu chưa có tên bài không gọi tìm/phát. Phép thử không
  thực thi công cụ nhạc và không đánh giá ASR. Chưa nạp bản thu mic lên board.
- Mục tiêu cần thử trên board: nói bình thường cách 1–2 m trong phòng yên.
  Đây chưa phải khoảng cách đã được đo/xác nhận. Không tăng thêm gain codec;
  gain phần mềm không phục hồi được tiếng đã chìm trong nhiễu hoặc ADC bị clip.
- Frontend mới có kiểm thử tín hiệu yếu, chuyển đột ngột sang lớn, nhiễu
  mức thấp, DC, cực trị int16, khung lẻ và reset sau mute. Đây là PCM tổng hợp,
  chưa chứng minh độ chính xác nhận tên bài hay khả năng thu xa của mic thật.
- `MIC level` ghi mỗi 5 giây đang thu hoặc trước lượt loa nếu đã thu ít nhất
  200 ms: RMS/peak đầu vào và đầu ra, gain cuối cửa sổ, `near_fs` là tỷ lệ
  mẫu đầu vào gần full-scale (phần nghìn). Log chỉ ghi thống kê, không ghi âm.
  So sánh phòng im và cùng một câu ở 0,5 / 1 / 2 m; nếu RMS khi nói gần bằng
  nền, cần kiểm tra lỗ mic/hướng lắp hoặc giải pháp mic khác. Peak gần 32768
  và `near_fs` lớn khi nói là dấu hiệu cần xem lại gain codec/đầu vào.
- Mic frontend không học mức loa: reset lúc đổi mute và mở phiên mới.
  Chạm tạm dừng nhạc, chờ màn hình nghe rồi mới nói; chưa có AEC.
- Prompt/công cụ tìm bài dùng phần tên đã nghe để tìm trước khi hỏi lại.
  Chỉ hỏi phần thiếu hoặc tối đa hai lựa chọn nếu còn mơ hồ; không tự phát
  bài đầu tiên không khớp. Đây là hướng dẫn model, cần thử hành vi Live thực tế.
- Tham khảo VAD: [Google StartSensitivity](https://googleapis.github.io/js-genai/release_docs/enums/types.StartSensitivity.html),
  [EndSensitivity](https://googleapis.github.io/js-genai/release_docs/enums/types.EndSensitivity.html),
  [AutomaticActivityDetection](https://googleapis.github.io/js-genai/release_docs/interfaces/types.AutomaticActivityDetection.html).

## API key và Git

`main/secrets.h` chỉ tồn tại trên máy phát triển, được include riêng trong
`gemini.c`. Kiểm tra quy tắc bỏ qua bằng `git check-ignore -v main/secrets.h`.
`main/gemini.h` chỉ còn model và API công khai, không chứa key thật.

`.gitignore` không xóa bí mật đã commit trong lịch sử. Repo có commit cũ
chứa key; nếu commit đó từng được chia sẻ, thu hồi key cũ và tạo key thay thế
trong Google AI Studio. Tách file không tự thu hồi key hoặc xóa lịch sử Git.
Key hiện vẫn được biên dịch vào firmware; không chia sẻ firmware chứa key thật.
