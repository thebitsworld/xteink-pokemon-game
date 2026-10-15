# Đoán mã

## Mục tiêu
Một người giấu một mã bí mật: một hàng gồm 4 hoặc 5 ký hiệu. Người kia đoán để phá mã. Mỗi lần đoán được chấm bằng các chốt cho biết đoán gần đúng đến đâu, và phải tìm ra mã trong số lần đoán cho phép (mặc định 10 lần). Bạn có thể phá mã của máy, hoặc giấu một mã và xem máy phá mã của bạn.

## Các ký hiệu
Có 6 ký hiệu, hoặc 8 trong các ván lớn hơn: hình tròn đặc, vòng tròn, hình vuông đặc, hình vuông rỗng, tam giác, dấu nhân, ngôi sao và hình thoi. Ở một số ván, một ký hiệu có thể xuất hiện nhiều lần trong mã ("được lặp").

## Cách chấm
Mỗi lần đoán nhận một chốt cho mỗi ký hiệu đúng:
- Chốt đen (chấm đặc): đúng ký hiệu, đúng vị trí.
- Chốt trắng (vòng tròn): đúng ký hiệu, sai vị trí.
- Chấm nhỏ là lỗ trống: không có gì thêm.

Các chốt không cho biết là ký hiệu nào. Một ký hiệu chỉ được tính bằng số lần nó có trong mã: đoán bốn hình tròn với mã chỉ có một hình tròn thì được một chốt.

## Các kiểu chơi
- Easy (Dễ): 4 vị trí, 6 ký hiệu, không lặp.
- Normal (Thường): 4 vị trí, 6 ký hiệu, được lặp.
- Hard (Khó): 5 vị trí, 8 ký hiệu, được lặp.
- Custom (Tùy chọn): cỡ mã chọn trong Options.
- Device breaks your code (Máy phá mã của bạn): máy đoán một mã có cỡ chọn trong Options.

Options còn chọn số lần đoán (8, 10 hoặc 12) và cách chấm các lần đoán của máy. Với "by the app" bạn nhập mã của mình trước và ứng dụng tự chấm từng lần đoán, không sợ nhầm. Với "by you" bạn giữ mã trong đầu và tự chấm từng lần đoán. Nếu các lần chấm mâu thuẫn nhau thì không mã nào khớp được: máy sẽ báo và bạn chấm lại lần đoán cuối. Mục "Undo last score" trong menu cho phép rút lại các lần chấm trước đó.

Menu hiện số ván thắng, số lần đoán trung bình và kỷ lục của bạn cho từng cỡ mã. Ván đang chơi được giữ lại khi bạn thoát.

## Điều khiển (nút bấm X3/X4)
- Trái và Phải di chuyển qua các vị trí, đến nút Submit.
- Lên và Xuống đổi ký hiệu tại con trỏ.
- Confirm gửi lần đoán.
- Khi tự chấm: Trái và Phải chọn Black, White hoặc Score it; Lên và Xuống đổi số chốt; Confirm để chấm.
- Back mở menu. Hết ván, Confirm để chơi lại.

## Điều khiển (cảm ứng X4 Pro)
- Chạm vào một vị trí, rồi chạm một ký hiệu ở hàng ký hiệu dưới bàn. Con trỏ tự sang vị trí trống tiếp theo.
- Chạm Submit guess. Khi tự chấm, chạm số chốt đen và chốt trắng, rồi chạm Score it.
- Chạm Menu ở phía trên để mở menu.

## Mẹo
- Hãy mở đầu bằng hai ký hiệu, mỗi loại hai cái (tròn, tròn, vòng, vòng): điểm của nó thu hẹp khả năng rất nhanh.
- Mỗi lần đoán mới nên khớp với tất cả các lần chấm trước, trừ khi một lần đoán với ký hiệu khác cho bạn biết nhiều hơn.
- Không được chốt nào là tin tốt: các ký hiệu đó không có trong mã.
- Ở cỡ 4 x 6 được lặp, máy dùng phương pháp của Knuth và không bao giờ cần quá năm lần đoán.
