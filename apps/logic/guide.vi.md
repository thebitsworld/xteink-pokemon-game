# Câu đố logic

Ba trò đố vốn chơi bằng giấy bút, được máy tạo mới mỗi lần. Câu đố nào cũng chỉ có đúng một lời giải và có thể tìm ra bằng suy luận, không cần đoán. Chọn một loại đố, rồi chọn cỡ. Mỗi loại giữ lại một ván đang chơi khi bạn rời đi (chọn Continue để chơi tiếp), và đếm số câu bạn đã giải cùng thời gian nhanh nhất. Đồng hồ dừng khi đang mở menu.

## Hashi (nối cầu)

Mỗi vòng tròn là một hòn đảo. Con số trên đảo cho biết có bao nhiêu cây cầu nối vào nó.

- Cầu chạy thẳng, ngang hoặc dọc, giữa hai đảo mà ở giữa không có gì chắn.
- Hai đảo nối với nhau bằng một hoặc hai cây cầu, không hơn.
- Các cây cầu không được cắt nhau.
- Khi đảo nào cũng đủ số cầu, mọi đảo phải liền thành một mạng duy nhất.

Đảo đã đủ cầu chuyển sang màu đen; đảo thừa cầu có viền dày.

## Lights Out (tắt đèn)

Bấm một ngọn đèn sẽ đảo trạng thái của nó và bốn đèn kề bên (đang sáng thành tắt, đang tắt thành sáng). Hãy tắt hết mọi đèn. Số lần bấm được đếm lại. Bàn nào cũng giải được, vì nó được tạo bằng cách bấm vài đèn trên một bàn tối.

## Slitherlink (vòng khép kín)

Vẽ một vòng khép kín duy nhất đi theo các đường chấm. Con số trong ô cho biết vòng đi qua bao nhiêu cạnh của ô đó; ô không có số thì tùy ý.

- Vòng không tự cắt, không tự chạm và không có đầu hở.
- Đánh dấu chéo một cạnh khi bạn chắc vòng không đi qua đó.

Số đã đủ cạnh chuyển sang nét mảnh; số bị thừa cạnh hiện chữ trắng trên nền đen.

## Điều khiển (nút bấm X3/X4)

- Back mở menu: Resume (chơi tiếp), Check for mistakes (đếm cầu, nét và dấu chéo sai), Restart (xóa bàn, đồng hồ vẫn chạy), New puzzle (câu mới), Other puzzles (loại đố khác).
- Hashi: các phím mũi tên đưa con trỏ từ đảo này sang đảo khác. Confirm chọn đảo; sau đó một phím mũi tên thêm cầu về hướng đó: một, hai, rồi lại không. Confirm hoặc Back để bỏ chọn đảo.
- Lights Out: mũi tên di chuyển con trỏ, Confirm bấm đèn.
- Slitherlink: con trỏ nằm trên một dấu chấm, mũi tên đưa nó đi dọc các cạnh. Confirm đổi bút: Move chỉ di chuyển; Line vẽ nét trên cạnh vừa đi qua (đi lại trên nét cũ sẽ xóa nó); Cross đánh dấu chéo theo cùng cách.
- Giải xong, bấm Confirm để sang câu tiếp theo.

## Điều khiển (cảm ứng X4 Pro)

- Chạm Menu để mở menu.
- Hashi: chạm một đảo rồi chạm đảo kề bên để thêm cầu; làm lại để thành hai cầu, lần thứ ba để bỏ cầu. Chạm vào khoảng nước giữa hai đảo cũng có tác dụng như vậy.
- Lights Out: chạm vào đèn.
- Slitherlink: chạm một cạnh để đổi lần lượt: nét, dấu chéo, trống.
- Giải xong, chạm màn hình để sang câu tiếp theo.

## Mẹo

- Hashi: đảo số 8 cần hai cầu ở mỗi hướng, đảo số 4 chỉ có hai đảo kề thì cần hai cầu tới mỗi đảo. Hễ con số bằng hai lần số đảo kề, mọi cây cầu của nó đều đã rõ.
- Hashi: không được nối hai đảo số 1 với nhau, cũng không nối hai đảo số 2 bằng cầu đôi: cặp đảo đó sẽ bị tách khỏi phần còn lại.
- Lights Out: làm từng hàng. Bấm đèn ngay bên dưới mỗi đèn còn sáng ở hàng trên. Nếu hàng cuối vẫn còn đèn sáng, chơi lại và thử bấm khác ở hàng trên cùng.
- Slitherlink: ô số 0 có dấu chéo ở cả bốn cạnh. Hai ô số 3 nằm cạnh nhau thì cạnh chung ở giữa chắc chắn có nét. Mỗi dấu chấm có hai nét hoặc không có nét nào.
