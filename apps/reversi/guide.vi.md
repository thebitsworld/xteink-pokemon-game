# Reversi

## Mục tiêu
Reversi chơi bằng các quân cờ tròn, một mặt đen, một mặt trắng. Quân đen vẽ đặc, quân trắng vẽ dạng vòng tròn rỗng. Khi cả hai bên đều không còn nước đi, ai có nhiều quân trên bàn hơn là người thắng.

## Luật chơi
- Ván bắt đầu với bốn quân ở giữa bàn, mỗi màu hai quân. Black (quân đen) đi trước.
- Đặt một quân sao cho nó kẹp một hàng thẳng quân đối thủ giữa nó và một quân khác của bạn: theo hàng ngang, hàng dọc hoặc đường chéo. Mọi hàng bị kẹp như vậy đều lật sang màu của bạn, cùng lúc theo cả tám hướng.
- Nước đi phải lật được ít nhất một quân. Ô không lật được quân nào thì không được đặt.
- Nếu bạn không có nước đi hợp lệ, bạn phải bỏ lượt và đối thủ đi tiếp. Dòng chữ dưới bàn cờ sẽ báo khi có người bỏ lượt.
- Khi cả hai bên đều không đi được (thường là lúc bàn đã đầy), ván kết thúc và đếm quân. Bằng số quân là hòa.
- Quân vừa đặt có một ô vuông nhỏ ở giữa. Số quân mỗi bên hiện ở phía trên; bên đang tới lượt có khung đậm hơn.

## Bắt đầu ván
- vs Device: chơi với máy ở mức Easy, Normal hoặc Hard. Số ván thắng, thua, hòa với từng mức hiện dưới nút của mức đó.
- Two players: hai người chơi lần lượt trên cùng một máy đọc.
- Options: chọn màu của bạn khi chơi với máy (Black đi trước) và có hiện các ô đi được bằng chấm nhỏ hay không (Show moves).
- Resume: chơi tiếp ván đang dở. Ván chưa xong được giữ lại khi thoát ứng dụng.
- Take back a move: quay lại lượt trước của bạn.

## Điều khiển (nút bấm X3/X4)
- Khi bật Show moves, Left và Right đưa con trỏ tới ô đi được trước hoặc sau, Up và Down tới ô đi được gần nhất ở hàng phía trên hoặc phía dưới. Con trỏ không bao giờ dừng ở ô không đi được.
- Khi tắt Show moves, các phím mũi tên di chuyển con trỏ tự do trên bàn, bạn phải tự tìm ô đi được.
- Confirm đặt quân vào ô con trỏ. Back mở menu.
- Khi ván đã kết thúc, Confirm bắt đầu ván mới cùng mức.

## Điều khiển (cảm ứng X4 Pro)
- Chạm vào một ô để đặt quân.
- Chạm Menu (góc trên bên phải) để mở menu.
- Khi ván kết thúc, chạm vào bàn cờ để chơi ván mới.

## Mẹo cho người mới
- Quân ở góc không bao giờ bị lật. Có cơ hội thì chiếm góc.
- Tránh các ô sát góc còn trống, nhất là ô chéo sát góc: chúng thường mở đường cho đối thủ chiếm góc.
- Lật thật nhiều quân ở đầu ván hiếm khi có lợi. Hãy giữ ít quân nhưng nhiều nước đi, và cố khiến đối thủ còn ít nước đi.
- Cạnh bàn là chỗ mạnh, nhưng chỉ khi đối thủ không chen vào phía sau được.
- Cuối ván hãy đếm kỹ: vài nước cuối quyết định tỉ số.
