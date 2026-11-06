# Khảo sát NhacCuaTui cho tính năng nhạc trực tuyến

Ngày khảo sát: 06/11/2026. Chỉ khảo sát HTTP và mã JavaScript công khai;
chưa tích hợp vào firmware, chưa giải mã/phát nhạc trên ESP32.

## Kết quả thực nghiệm

Các yêu cầu thử không dùng tài khoản, cookie, Authorization, API key, Origin
hoặc Referer. Truy vấn đầu dùng User-Agent Mozilla/5.0; truy vấn thứ hai,
chi tiết bài hát và kiểm tra MP3 dùng User-Agent mặc định của Python urllib.
Các kết quả này mô tả hành vi quan sát được, chưa xác nhận là API đối tác
công khai hay có cam kết tương thích từ NCT.

| Yêu cầu | Kết quả |
| --- | --- |
| `GET /api/v3/search/all?keyword=...&correct=true&timestamp=1793978518324` | HTTP 200, JSON `code: 0`, khoảng 57,6 KB |
| Cùng tìm kiếm, bỏ `timestamp` | HTTP 200, `code: 0`; có `data.songs` và `streamURL` |
| Tìm `Nơi Này Có Anh`, không timestamp | HTTP 200; kết quả có bản Sơn Tùng M-TP và các bản remix |
| `GET /api/v1/search/song?keyword=...&pageindex=1&pagesize=3&correct=false` | HTTP 200, `code: 0`, `data.songs` có 3 bài; khoảng 8,9 KB |
| `GET /api/v1/song/detail/utXSBhTqw0Jk` | HTTP 200, `code: 0`, `data` là thông tin bài và các luồng; khoảng 3,1 KB |
| `GET` luồng 128 kbps với `Range: bytes=0-8191` | HTTP 206, `Content-Type: audio/mpeg`, `Accept-Ranges: bytes` |

Host API: `https://graph.nhaccuatui.com`.
`keyword` phải được URL-encode theo UTF-8. Tham số `timestamp` không bắt buộc
trong các lần thử này; chưa xác định ngữ nghĩa chính xác của `correct`.

Hai endpoint v1 được tìm thấy trong JavaScript do trang chủ NCT tải, sau đó
được gọi thử trực tiếp. Bundle website còn khai báo `searchAll` bằng POST;
GET của URL người dùng cung cấp hoạt động trong khảo sát này.

## Dữ liệu bài hát

Kết quả đứng đầu của truy vấn người dùng:

```json
{
  "key": "utXSBhTqw0Jk",
  "name": "Như Một Người Rưng",
  "artistName": "OSAD, Hà An Huy",
  "duration": 177,
  "statusPlay": 1,
  "streamURL": [
    {"type": "128", "status": 1, "onlyVIP": false, "stream": "<URL HTTPS do NCT tra ve>"},
    {"type": "320", "status": 1, "onlyVIP": false, "stream": "<URL HTTPS do NCT tra ve>"},
    {"type": "lossless", "status": 0, "onlyVIP": true, "stream": "<URL HTTPS do NCT tra ve>"}
  ]
}
```

URL phát có các tham số như `st`, `e`, `r`; không lưu cứng hoặc tự sửa chữ ký.
Cần lấy URL mới từ chi tiết bài khi bắt đầu phát và xử lý URL không còn hợp
lệ. Trong phạm vi khảo sát chỉ thử luồng 128 được trả là `onlyVIP: false`,
`status: 1`; không thử luồng VIP hoặc không khả dụng. Có URL trong JSON
không tự chứng minh quyền hay khả năng phát toàn bộ mọi bài.

## Kiểm tra định dạng MP3

Chỉ đọc các đoạn nhỏ, không tải toàn bài và không phát ra loa:

- HTTP 206; file được server báo có 3.079.866 byte.
- Đầu file là ID3v2.3, tổng header/metadata 241.797 byte. Đọc thêm 4.096 byte
  sau phần này và xác nhận hai header frame MP3 liên tiếp.
- MPEG-1 Layer III, 128 kbps, 44.100 Hz, 2 kênh.
- Bộ đọc MP3 cần bỏ qua metadata lớn, không đợi nhét toàn ID3 vào bộ đệm
  frame nhỏ. Có thể đọc bỏ dần; Range là khả năng đã quan sát ở CDN mẫu.

Firmware hiện dùng I2S chung cho mic/loa ở mono 16.000 Hz. Khi tích hợp phải
giải mã MP3, trộn stereo thành mono và đổi tần số có lọc xuống 16.000 Hz.
Không đẩy trực tiếp MP3 hoặc PCM 44.100 Hz vào `speaker_queue_pcm24()`, vì
hàm này chỉ nhận PCM 24.000 Hz của Gemini. Giữ đầu ra 16 kHz sẽ giới hạn
dải tần nghe nhạc; tăng chất lượng là thay đổi riêng của luồng âm thanh.

## Thiết kế đề xuất

1. `search_music(query)`: gọi endpoint chỉ tìm bài hát, lấy tối đa 3–5 kết
   quả; trả cho Gemini mã bài, tên, nghệ sĩ, thời lượng và cờ khả dụng.
   Giữ URL/kết quả đầy đủ tại firmware, không nhờ model tự tạo URL phát.
2. `play_music(track_id)`: chấp nhận mã thuộc kết quả đã tìm; gọi chi tiết
   bài để lấy URL mới, chọn luồng khả dụng không yêu cầu VIP, ưu tiên 128.
3. Task tải HTTP và giải mã chạy ngoài callback WebSocket, dùng bộ đệm giới
   hạn. Bộ quản lý audio cho một nguồn ghi I2S tại một thời điểm; xác định
   thứ tự giữa lời xác nhận của Gemini và bài hát trước khi bắt đầu phát.
4. Cập nhật màn hình: đang tìm, tích đệm, đang phát, tạm dừng hoặc lỗi;
   chỉ báo đang phát sau khi loa bắt đầu nhận PCM.
5. Xử lý dừng/chuyển bài bằng hủy nguồn và loại bộ đệm cũ; dùng nút cảm ứng
   tạm dừng/tiếp tục/dừng. Firmware hiện chưa có AEC nên chưa
   bảo đảm nghe lệnh giọng nói trong lúc phát nhạc.
6. Kiểm tra trên ESP: TLS đồng thời với Gemini, RAM, thiếu dữ liệu mạng,
   metadata lớn, URL hết hiệu lực, file lỗi, tìm sai bài và chuyển lại hội
   thoại. Không tuyên bố đã chạy được trên board từ kết quả HTTP trên PC.

Về kỹ thuật có thể thử hướng ESP gọi API và CDN trực tiếp; backend không
còn là điều kiện bắt buộc theo kết quả hiện tại. Backend vẫn là một lựa
chọn nếu cần thay nguồn hoặc xử lý âm thanh ở ngoài ESP. Endpoint website
có thể thay đổi; nên gom phần NCT vào một module riêng để dễ cập nhật.

## Nguồn và bằng chứng

- [Endpoint người dùng cung cấp](https://graph.nhaccuatui.com/api/v3/search/all?keyword=Nh%C6%B0%20M%E1%BB%99t%20Ng%C6%B0%E1%BB%9Di%20R%C6%B0ng&correct=true&timestamp=1793978518324)
- [Tìm riêng bài hát](https://graph.nhaccuatui.com/api/v1/search/song?keyword=Nh%C6%B0%20M%E1%BB%99t%20Ng%C6%B0%E1%BB%9Di%20R%C6%B0ng&pageindex=1&pagesize=3&correct=false)
- [Chi tiết bài mẫu](https://graph.nhaccuatui.com/api/v1/song/detail/utXSBhTqw0Jk)
- [Trang chủ NhacCuaTui](https://www.nhaccuatui.com/) và bundle JavaScript
  công khai được tham chiếu từ HTML trang chủ.

Phản hồi thô và tóm tắt kiểm tra nằm ở `build/nct_research/` (đã được Git
bỏ qua). Báo cáo không chép các URL CDN có chữ ký vào mã nguồn.
