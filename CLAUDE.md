# CLAUDE.md — Session Handoff Notes

Ghi chú ngữ cảnh cho Claude ở phiên làm việc sau (hoặc trên máy khác) để tiếp tục công việc mà không phải khám phá lại từ đầu. File này KHÔNG phải tài liệu chính thức của dự án — chỉ là sổ tay bàn giao, có thể dọn/xóa khi công việc đang dở đã xong.

## Dự án là gì

Đây là fork của **CrossInk** (firmware ESP32-C3 cho máy đọc sách e-ink Xteink X3/X4), được mở rộng thêm **module game Pokémon** — Pokémon lên cấp dựa trên thời gian đọc sách thật (không phải cày game thuần túy). Xem [docs/pokemon-game.md](docs/pokemon-game.md) (tài liệu người dùng) và [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md) (tài liệu kỹ thuật chi tiết — **đọc file này trước** để hiểu cơ chế game).

## Bối cảnh đã khảo sát (phiên 2026-09-04)

1. **Đọc sâu toàn bộ module Pokémon** (`lib/Pokemon/*`, `src/pokemon/*`, `src/activities/pokemon/*`) và giải thích chi tiết cơ chế: đo thời gian đọc chống gian lận (`PokemonTracker`), vòng lặp XP/encounter/item/evolution (`PokemonGame.cpp::applyCreditedMinutes`), lưu trữ double-buffer + CRC (`PokemonStore`). Toàn bộ đã chép lại vào [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md).
2. **Phát hiện quan trọng về gameplay**: `bookProgressPercent` (dùng để gate độ hiếm/level wild encounter) là % tiến độ của **cuốn sách đang đọc hiện tại**, không phải tổng thời gian/trang đã đọc lũy kế → đọc nhiều sách ngắn đạt % cao nhanh hơn, cho encounter tốt hơn với cùng lượng thời gian đọc thật so với đọc một cuốn dài. Đây có thể là điểm cần cân bằng lại nếu muốn sửa.
3. **Đã cài đặt môi trường build thành công**:
   - PlatformIO đã cài ở `~/.platformio/penv/bin/pio` nhưng **không có trong PATH mặc định** của shell — phải `export PATH="$HOME/.platformio/penv/bin:$PATH"` trước khi chạy `pio`.
   - Submodule `freeink-sdk` **chưa init lúc bắt đầu phiên** (thư mục rỗng) → phải chạy `git submodule update --init --recursive` trước khi build (nếu không sẽ lỗi `PackageException: Can not create a symbolic link`).
4. **Build thật `pio run -e pokemon-x3` và `pio run -e default` để đo dung lượng** — số liệu thật (không phải ước lượng):

   | Build | Flash dùng | % | Free trong OTA slot (6.25MB) |
   |---|---|---|---|
   | `default` (không Pokémon) | 6,259,237 B | 95.5% | 280,208 B |
   | `pokemon-x3` (có Pokémon) | 6,303,483 B | 96.2% | 235,968 B |

   - RAM tĩnh chỉ dùng 17.7% (327,680 bytes tổng) — **không phải nút thắt**.
   - **Flash mới là nút thắt thật sự**: chỉ còn ~230KB trống trước khi `scripts/check_firmware_size.py` (post-build check trong `platformio.ini`) fail build.
   - Module Pokémon hiện tại chỉ tốn ~43KB flash (vì artwork nằm trên SD card, không đóng gói vào firmware).
5. **Đổi git remote `origin`** từ `https://github.com/padge01/xteink-pokemon-game.git` sang `https://github.com/thebitsworld/xteink-pokemon-game.git` theo yêu cầu người dùng. Đã `git fetch origin` thành công (repo tồn tại, kết nối được) nhưng **chưa kiểm tra remote mới có khác gì so với code hiện tại (branch `main` local vẫn đang track cấu hình cũ, chưa merge/rebase gì)**.
6. **Tạo file tài liệu kỹ thuật**: [docs/development/pokemon-mechanics.md](docs/development/pokemon-mechanics.md) — đã commit (`aaec0c59` trên `main`).

## Trạng thái git

- `main` có commit `aaec0c59` (tài liệu cơ chế + CLAUDE.md) — **chưa push** vì máy này chưa có credential GitHub nào hoạt động (chưa cài `gh`, SSH key `~/.ssh/id_ed25519` chưa đăng ký với GitHub, không có credential helper HTTPS).
- Remote `origin` đã đổi sang `https://github.com/thebitsworld/xteink-pokemon-game.git`. Đã `git fetch` được, nhưng **chưa đối chiếu xem remote có khác gì so với local**.
- Đang làm việc trên nhánh `feat/pokemon-battle-system` (tách từ `main`).
- Chạy `git status` để biết chính xác cái gì chưa commit.

## Việc đang dở: hệ thống chiến đấu Pokémon

Đang triển khai trên nhánh **`feat/pokemon-battle-system`**. Kế hoạch đầy đủ, ràng buộc kỹ thuật, quyết định thiết kế kèm lý do, và danh sách task chia theo giai đoạn đều nằm ở:

👉 **[docs/development/pokemon-battle-roadmap.md](docs/development/pokemon-battle-roadmap.md)** — đọc file này trước khi viết code.

Phạm vi: học chiêu theo level (dữ liệu Red thật), vật phẩm + TM/HM rơi khi đọc sách, chiến đấu turn-based đầy đủ status effect, bắt Pokémon bằng 4 loại bóng, 8 gym + Elite Four theo thứ tự, màn hình huy hiệu.

**Tiến độ**: **Toàn bộ 8 giai đoạn của roadmap chiến đấu đã xong**, cộng thêm **GĐ9-17 ngoài roadmap gốc** (commit `b4d21f31`, `eff18d13`, `f1ff1bd2`, `da111328`, `18a0eeca`, `a989d161`, `2f420118`, `5f496ad6`, `ddef31b0`, `62fd5028`, `194601a0`, `7605de43`, `060d272f`, `2fc7af7a`, `a554d92f`, `c3998aa9`, `22bb4638`, `5b9d24d2`, `19268f61`, `f2187062`, `ebb7e9fd`, `4d4f6bbb`, `950d72ce`, `9d433ad0`, `05b623ed`, `491411e7`, `0477355e`, `470120d1`, `9b12bae6`, `188b005d` trên `feat/pokemon-battle-system`). **Push đang do người dùng tự làm thủ công** — máy này không có credential GitHub hoạt động (xem "Trạng thái git" bên dưới), đừng tự ý thử push nữa trừ khi được yêu cầu lại.

**Điểm dừng phiên (2026-09-08)**: người dùng tự chạy giả lập, phản hồi qua nhiều vòng chỉnh màn Battle (GĐ16→GĐ17→2 bản sửa tiếp), cuối cùng xác nhận **"đẹp rồi. tạm thời dừng ở đây"**. Working tree sạch (`git status` không có gì chưa commit), tất cả đã commit lên `feat/pokemon-battle-system`, sẵn sàng để người dùng tự push. Việc **chưa xác nhận bằng mắt**: HP bar mới thêm ở `Screen::Party` (dòng 96px) và `Screen::Summary` (GĐ17) — người dùng mới xác nhận màn Battle, chưa nói gì về 2 màn này; nếu phiên sau nhận phản hồi cần chỉnh, đọc mục "GĐ 17" trong roadmap trước.

**Kế hoạch đang chờ triển khai (ghi 2026-09-09, CHƯA có code nào)** — đọc mục **"Kế hoạch 3 giai đoạn tiếp theo (GĐ18-20)"** ở cuối roadmap trước khi bắt đầu, đã khảo sát code thật (không đoán) và ghi sẵn file:line cụ thể:
- **GĐ18**: thêm nút "BAG" vào `Screen::Battle` để dùng Potion/thuốc giải status **giữa trận** (hiện chưa có đường vào nào cả) — điểm khó chính đã ghi rõ trong roadmap: `useConsumable()` ghi thẳng xuống `BattleRecordEntry` trên đĩa qua `recordId`, không tự đồng bộ vào `battlePlayer_` (RAM) đang dùng bởi engine/renderer — cần gọi lại `peekBattleMoves()` để đồng bộ sau khi dùng item. Cố tình loại Candy khỏi phạm vi (tăng level giữa trận phức tạp hơn).
- **GĐ19**: thêm `Screen::BagBalls` để **xem** số lượng 4 loại bóng ngoài trận (hiện bóng hoàn toàn không có chỗ xem ngoài `Screen::BattleBalls` lúc đang bắt). Phần "rớt bóng qua đọc sách" và "bóng không hoạt động ở gym battle" **đã có sẵn từ trước, đã xác nhận qua code** — không cần sửa, chỉ build lại rồi xác nhận.
- **GĐ20**: script Python chỉnh trực tiếp `fs_/.crosspoint/pokemon-{a,b}.bin` (giả lập) để chèn sẵn 1 `PendingEventKind::Encounter` (offset/luật hợp lệ đã ghi chi tiết trong roadmap), giúp test màn bắt Pokémon ngay không cần chờ đọc sách đủ lâu. Đây là công cụ dev, không phải tính năng.

**GĐ17 (chấm tròn còn sống trong Battle + HP bar cho Party/Summary, commit `470120d1` + 2 bản sửa `9b12bae6`/`188b005d`, ngoài roadmap gốc — phản hồi sau khi chơi thử GĐ16)**: khung HP trong Battle thu nhỏ (bỏ tên loài ban đầu, rồi thêm lại theo phản hồi — xem 2 bản sửa), thêm hàng chấm tròn phía trên mỗi khung thể hiện số Pokémon còn sống — chấm đặc=còn sống, chấm rỗng+X=đã gục (dùng `fillRoundedRect`/`drawRoundedRect` với cornerRadius=size/2 vì GfxRenderer không có API vẽ tròn riêng); đối thủ tính theo `gymTeamFor()`+`gymChallengeTeamProgress_` khi đấu gym (1 chấm khi hoang dã), player tính theo `snapshot_.partyCount` qua `peekBattleMoves()`. `Screen::Party` dòng cao hơn (64→96px, chỉ riêng màn này qua `rowHeightForScreen()` mới) để thêm dải HP bar+text+status ở đáy mỗi dòng (`renderPartyRowHealth()` mới). `Screen::Summary` thêm 1 dòng HP bar+text dưới dòng Number/Level/Gender. **Bản sửa 1** (`9b12bae6`): thêm lại tên/nickname vào khung HP (bị bỏ nhầm ở bản đầu), `panelHeight` 50→72, sửa kèm bug `zoneContentHeight` (panel cao hơn sprite thì phải tính lại đúng vùng). **Bản sửa 2** (`188b005d`): khung HP hẹp lại theo chiều ngang + cao hơn theo chiều dọc (`panelWidth` cố định 220, `panelHeight` 72→92), HP bar+text gộp cùng hàng và căn giữa theo chiều dọc trong khung, hộp log trận đấu đổi sang chiều cao cố định 90px (trước đó chiếm hết phần còn lại) để nhường diện tích — khoảng trống giải phóng dồn vào khe hở giữa 2 vùng đấu. Chỉ sửa `PokemonActivity.cpp/.h`. Flash cuối cùng **6,346,287 B/96.8%, còn 193,168 B**. **Đã xác nhận bằng mắt** — người dùng nói "đẹp rồi" sau bản sửa 2.

**GĐ16 (vẽ lại màn Battle theo phong cách Pokémon Red, commit `0477355e`, ngoài roadmap gốc — người dùng chê "màn hình battle xấu quá")**: `renderBattleHud()` viết lại hoàn toàn theo bố cục chéo kinh điển của Red — hộp tên/level/HP đối thủ góc trên-trái + art đối thủ góc trên-phải; art player góc dưới-trái + hộp tên/level/HP player góc dưới-phải. Hộp bo góc (`drawRoundedRect`), nhãn "HP" trước thanh máu. Tọa độ tính động theo `listBounds_.y` (đỉnh menu FIGHT/BALL/SWITCH/RUN) nên tự thích ứng khi `rowCount_` đổi (gym 3 dòng vs hoang dã 4 dòng). Thêm hộp thoại nhật ký bo góc riêng phía trên menu, chữ căn trái. Thêm lời mở màn "Go, X!"/"leader sent out X!" (trước đây màn hình trống tới hết lượt đầu) — 1 key i18n mới `STR_POKEMON_SENT_OUT`. Chỉ sửa `PokemonActivity.cpp`+`english.yaml`. Flash **6,345,083 B/96.8%, còn 194,368 B**, +1,010 B. Bố cục ban đầu này sau đó được tinh chỉnh tiếp ở GĐ17 (2 bản sửa) theo phản hồi thực tế.

**GĐ15 (double-buffer cho `pokemon-battle.bin`, commit `491411e7`, ngoài roadmap gốc — người dùng chỉ ra giả định nền GĐ3 đã sai)**: file phụ lưu HP/PP/moveset trận đấu được thiết kế GĐ3 **không** double-buffer vì "tái tạo được 100% từ level+learnset" — nhưng từ GĐ7/11/12 (học chiêu tự động, màn Moveset chủ động học/quên, dạy TM/HM), **moveset là lựa chọn thật của người chơi**, không còn suy ra được từ level. Đổi 1 file `pokemon-battle.bin` thành 2 file luân phiên `pokemon-battle-{a,b}.bin` đúng triết lý `PokemonStore` (main save): ghi luôn nhắm file đang không active, đọc lại xác minh rồi mới chuyển active — file active không bao giờ bị đụng nên 1 lần ghi/crash dở dang không mất bản còn lại. Codec thêm header 10 byte (magic+version+entryCount+sequence). Migration tự động từ file đơn cũ (không xóa, giữ làm bản dự phòng). API công khai (`load/findEntry/upsertEntry/removeEntry`) không đổi nên `PokemonService` và 40+ test hiện có không cần sửa. Flash **6,344,073 B/96.8%, còn 195,376 B**, +1,518 B. 19/19 test pass (thêm test fallback khi 1 file hỏng, cả 2 hỏng vẫn an toàn, ghi thất bại không đụng file active, migration giữ đúng move đã dạy).

**GĐ14 (khôi phục đội hình đầy đủ gym/Elite Four, commit `9d433ad0`+`05b623ed`, ngoài roadmap gốc)**: đảo ngược quyết định GĐ12 "rút gym team còn 2-3 con vì chi phí refresh e-ink" — theo yêu cầu người dùng, giờ mọi gym/Elite Four dùng đúng đội hình thật Pokémon Red (Giovanni/Lorelei/Bruno/Agatha/Lance đều 5 con). Viết lại toàn bộ `scripts/data/pokemon-gyms.csv`, `MAX_GYM_TEAM_SIZE` 3→5. Species/move id đối chiếu bằng script đọc thẳng CSV, không dùng trí nhớ. Tiện thể sửa bug: đội Bruno gán nhầm species Hitmonchan (107) với moveset Hitmonlee — giờ cả hai loài Hitmon xuất hiện đúng là 2 thành viên riêng với moveset thật. Không cần sửa UI (`enterGymBattle()` đã lặp động theo `team.size()`). Flash **6,342,555 B/96.8%, còn 196,896 B**, +104 B. 19/19 test pass.

**GĐ13 (đổi Pokémon trong trận, commit `950d72ce`, ngoài roadmap gốc)**: gym battle/bắt hoang dã giờ chỉ thua khi **toàn bộ party hết HP** hoặc chủ động RUN, không còn thua ngay khi Pokémon đầu party gục. `Screen::Battle` thêm "Switch" → `Screen::BattleSwitch` (mới) chọn Pokémon khác còn HP; gục giữa trận mà party còn sống thì bắt buộc đổi (không cho hủy bằng Back). Giản lược có chủ đích: đổi Pokémon không tốn lượt (không mô phỏng "đối thủ đánh miễn phí khi đổi" như game gốc — cần sửa engine `stepBattle()` mới làm đúng luật này). Chỉ sửa `PokemonActivity.cpp/.h`. Flash **6,342,451 B/96.8%, còn 196,992 B**, +1,556 B. 19/19 test pass.

**GĐ12 (sửa 4 vấn đề sau playtest GĐ11, commit `5b9d24d2`, fix bug `f2187062`)**: (1) gym leader/Elite Four giờ dùng moveset thật của Pokémon Red — lấy qua WebFetch từng trang Bulbapedia, không đoán; `GymTeamMember` thêm trường `moves`, `setupBattleOpponent()` nhận `fixedMoves` thay vì luôn suy ra từ learnset; nhân tiện sửa 5 chỗ level sai (Elite Four). (2) TM/HM giờ lọc đúng theo hệ — nối `canLearnViaMachine()` (đã có sẵn từ trước, chỉ thiếu gọi) vào `teachMove()`. (3) Màn Moveset thêm "Forget" (xóa hẳn 1 ô, không cho xóa chiêu cuối); TM đầy 4 chiêu không còn chặn hẳn — mở `Screen::TmReplaceSlot` để chọn ô thay. (4) AI đối thủ (`chooseOpponentMove()`) bớt máy móc: 1/4 lượt cân nhắc mọi chiêu còn PP, hòa điểm hiệu quả thì chọn ngẫu nhiên thay vì luôn slot đầu — kết hợp mục (1) giải quyết "Onix chỉ đánh 1 chiêu". Flash **6,340,863 B/96.8%, còn 198,592 B**, +4,882 B. 19/19 test pass (thêm test AI-random 12-seed + gym-moveset assertion). Chi tiết đầy đủ ở mục "GĐ 12" cuối roadmap.

**GĐ11 (màn quản lý moveset, commit `a554d92f`, ngoài roadmap gốc — người dùng phản hồi không có cách chủ động đổi chiêu khi đã đủ 4)**: `Party > Actions > Moves` (mới, `CollectionAction::Moveset`) mở `Screen::Moveset` (xem 4 ô chiêu) → chọn ô → `Screen::MovesetPick` (mọi chiêu trong learnset chưa biết, ở cấp hiện tại trở xuống) → học đè vào ô đã chọn qua `PokemonService::learnMoveIntoSlot()` (mới). Khác 2 luồng cũ: TM (GĐ9) không cho thay khi đầy, MoveLearn tự động (GĐ7) chỉ hiện đúng lúc lên cấp — Moveset cho chủ động bất cứ lúc nào. Flash **6,335,981 B/96.7%, còn 203,472 B**, +1,316 B.

**GĐ10 (tên chiêu trên TM/HM + nút bên nhảy trang, commit `060d272f`, ngoài roadmap gốc)**: Bag>TM-HM hiện "TM01 - Mega Punch" thay vì chỉ "TM01". `PokemonActivity::loop()` tách 2 nút bên hông (Up/Down) = nhảy 1 trang (`ButtonNavigator::nextPageIndex`/`previousPageIndex`, có sẵn), 2 nút phía trước (Left/Right) = từng dòng như cũ — áp dụng cho mọi màn danh sách, không chỉ Bag. Chỉ sửa `PokemonActivity.cpp`, không đụng service/storage. Flash **6,334,665 B/96.7%, còn 204,784 B**, +672 B.

**GĐ9 (Bag phân loại category, commit `194601a0`, ngoài roadmap gốc — người dùng yêu cầu sau khi chơi thử simulator)**: `Screen::Bag` giờ là màn chọn category (Evolution/Medicine/TM-HM) thay vì gộp chung. `Screen::BagMedicine` mới + `PokemonService::useConsumable()` — **tính năng hoàn toàn mới**: trước đây Potion/status-cure/PP-restore/Candy không dùng được, chỉ có đá tiến hóa và TM. Giản lược có chủ đích: PPRestore hồi mọi ô chiêu (không phân biệt Ether/Elixir vì data không có field phân biệt); Candy tăng cấp nhưng không tự check tiến hóa (bắt được ở lần đọc sách kế tiếp). Flash **6,333,993 B/96.6%, còn 205,456 B**, +1,488 B. 19/19 test pass, thêm 5 test cho useConsumable. Chi tiết đầy đủ ở mục "GĐ 9"/"GĐ 10" cuối roadmap.

**Việc còn lại không phải code** — 2 mục Verification chỉ con người/thiết bị thật mới làm được (xem roadmap mục GĐ8 để biết chi tiết):
1. Chơi thử tương tác trên simulator có GUI thật (gặp wild→battle→bắt→xem Summary 4 chiêu→dùng TM→đánh gym→Badges→Elite Four) — máy build này không có màn hình/input ảo.
2. Test trên thiết bị X3 thật (tốc độ refresh e-ink, heap chật) — xem [pokemon-x3-build-and-flash.md](docs/development/pokemon-x3-build-and-flash.md).

**GĐ8 (hoàn thiện, commit `ddef31b0`)**: rà soát i18n xong (không cần thêm key, 65 "never used" là baseline có từ trước). `clang-format`: máy không có `clang-format-21` (CI), chỉ có v22.1.3 (bundle VSCode cpptools) — **chỉ format tay phần code do nhánh này viết** (GĐ0-7), không đụng code CrossInk gốc có từ trước để tránh làm lệch định dạng đã qua CI thật. Build sạch 3 env: `default` 6,261,393 B/95.5%; `pokemon-x3` **6,332,505 B/96.6%, còn 206,944 B**; simulator chạy được. **Tổng chi phí flash toàn bộ 8 giai đoạn: +29,022 B** so với baseline 6,303,483 B trước khi bắt đầu — rẻ hơn ước lượng ban đầu (~45-65KB).

**GĐ7 (Summary 4 chiêu + học chiêu + dạy TM, commit `5f496ad6`)**: `PendingEventKind::MoveLearn` (khai báo từ GĐ3, chưa dùng tới) giờ được sinh thật khi lên cấp và moveset đầy 4 ô (còn ô trống thì tự học, không hỏi). Summary hiện 4 chiêu qua `peekBattleMoves()` (đọc thuần, không ghi SD). Bag mở rộng gồm 55 vật phẩm Machine để dạy TM/HM (`teachMove()`) — **giản lược có chủ đích**: TM khi moveset đầy không có picker chọn-ô-thay (chỉ báo lỗi, không tiêu TM), khác với MoveLearn tự động (CÓ picker 5 dòng). Flash **6,332,505 B / 96.6%, còn 206,944 B (~202KB)**, +2,890 B so với GĐ6. 34/34 test PokemonServiceTest, 19/19 suite native tổng. **Gotcha khi viết test**: credit minutes nhảy cấp lớn trong 1 lần gọi có thể băng qua ngưỡng encounter/item (15/60 phút) làm hàng đợi pending-event bị event khác chiếm chỗ — luôn đặt `totalXp` thiếu đúng 1 XP rồi `creditMinutes(1,...)` khi test cần băng qua đúng 1 ngưỡng cấp độ cụ thể.

**GĐ6 (UI Gym List + Badges, commit `2f420118`)**: `Screen::GymList`/`Badges` + 2 mục menu. Chọn gym đang mở thực sự vào trận (tái dùng 3 màn Battle của GĐ5, không có BALL, đối thủ tung lần lượt cả đội 2-3 con, thắng hết mới `markGymDefeated`). Logic mở khóa tuyến tính gộp vào 1 hàm dùng chung `pokemon::gymProgressFor()` giữa service và UI. Flash **6,329,615 B / 96.6%, còn 209,840 B (~205KB)**, +2,816 B so với GĐ5. 19/19 test pass.

**GĐ5 (UI Battle + bắt bóng, commit `a989d161`)**: 3 màn `Battle`/`BattleMoves`/`BattleBalls` (không phải 4 — bỏ `BattleBag`, engine chưa có cơ chế dùng item giữa trận) nối vào nút Catch cũ. Flash **6,326,799 B / 96.5%, còn 212,656 B (~208KB)**. 19/19 test pass. **Gotcha quan trọng phát hiện ở GĐ này (vẫn áp dụng cho mọi GĐ sau)**: sửa `lib/I18n/translations/*.yaml` xong, `pio run` incremental có thể báo "up to date" mà KHÔNG rebuild `I18nStrings.o` (SCons không track qua `pre:` generator hook) → linker dùng object cũ với `StrId` enum layout cũ, tra sai chuỗi âm thầm không lỗi build. **Luôn `rm -rf .pio/build/<env>` sau khi sửa file yaml trong `lib/I18n/translations/`** trước khi tin số liệu build. Chi tiết đầy đủ trong roadmap mục GĐ5.

- GĐ 0: dữ liệu nguồn — `scripts/data/pokemon-{stats,moves,learnsets,tmhm}.csv` từ PokeAPI (151/165/989/3037 dòng) + `pokemon-gyms.csv` viết tay (8 gym + 4 Elite Four).
- GĐ 1: 5 generator C++ sinh header vào `$BUILD_DIR/generated/pokemon`, cộng struct thủ công `lib/Pokemon/PokemonBattleTypes.h` + 5 file accessor `.cpp`.
- GĐ 2: `lib/Pokemon/PokemonBattle.h/.cpp` (engine thuần: damage, 6 status, catch) + `lib/Pokemon/PokemonTypeChart.cpp` (bảng khắc chế 18×18 viết tay). Sửa 1 bug dữ liệu thật: `ailment_chance=0` của PokeAPI cho chiêu Status nghĩa là "chắc chắn 100%", không phải "không bao giờ".
- GĐ 3: save format v3 (`bagCounts[77]` + `battleProgress` append sau byte 115) + file phụ `/.crosspoint/pokemon-battle.bin` (`PokemonBattleStoreCodec` + `PokemonBattleStore`, không double-buffer vì tái tạo được 100%). `PendingEventKind::MoveLearn` mới.
- GĐ 4: `PokemonService` thêm `consumeBagItem`/`markGymDefeated`/`loadBattleEntry`/`saveBattleEntry` + hồi HP/PP khi đọc trong `creditMinutes()`. `createItem()` (PokemonGame.cpp) đổi sang chọn theo trọng số trên 83 vật phẩm — **thay đổi duy nhất** chạm PokemonGame.cpp.
- **Flash**: GĐ1/2 = 6,303,587 B (dữ liệu/engine chưa ai gọi, linker loại bỏ hết) → GĐ3 = 6,304,033 B (+446, state đổi thật) → **GĐ4 = 6,314,621 B (96.4%, còn 224,832 B ≈ 220KB), +10,588 B** vì service giờ thực sự gọi vào toàn bộ bảng dữ liệu. Đây là mốc chi phí thật đầu tiên đúng như roadmap dự đoán.
- 19/19 native test Pokémon pass, 25/25 trong `PokemonServiceTest` (`cd test && ctest -R Pokemon`, cần cmake của PlatformIO: `export PATH="$HOME/.platformio/packages/tool-cmake/bin:$PATH"` vì máy không cài cmake hệ thống).
- **Chưa chạy được simulator**: máy thiếu `libsdl2-dev`, không cài được vì `sudo` cần xác thực tương tác (không có trong môi trường non-interactive này). Nếu phiên sau có quyền sudo tương tác, `sudo apt-get install -y libsdl2-dev` rồi `pio run -e pokemon-simulator-X3 -t run_simulator` để kiểm chứng runtime thật.
- **Lưu ý cho GĐ5+ (UI)**: firmware hiện tại build được nhưng **chưa có màn hình nào dùng API mới** — service layer đã sẵn sàng nhưng vô hình với người chơi cho tới khi UI (GĐ5-7) nối vào.

**Bốn ràng buộc dễ gây hỏng nhất** (chi tiết trong roadmap):
1. Flash chỉ còn ~230KB — build đo lại sau mỗi giai đoạn.
2. `PokemonState` chỉ được append **sau byte 115** (`PokemonStore.cpp:265` hardcode offset 108 cho `sequence`).
3. **Không** nới `PokemonRecord` 48 byte — dữ liệu chiến đấu để ở file phụ riêng (`pokemon-battle-{a,b}.bin`, double-buffer từ GĐ15 vì moveset không còn tái tạo được từ khi có TM/Moveset screen; HP/PP/status vẫn tái tạo được).
4. Tên chiêu/vật phẩm **không** đi qua i18n (bảng offset nhân 28 ngôn ngữ + `strip_unused` không quét `$BUILD_DIR`).

## Cách build nhanh (đã verify hoạt động trong phiên này)

```sh
export PATH="$HOME/.platformio/penv/bin:$PATH"
cd /home/vutq/project/xteink-pokemon-game
git submodule update --init --recursive   # chỉ cần nếu freeink-sdk trống
pio run -e pokemon-x3    # firmware có Pokémon, cho X3/X4
pio run -e default       # firmware gốc, không Pokémon — dùng để so sánh dung lượng
pio run -e simulator      # build cho simulator (test nhanh, không cần thiết bị)
```

Số liệu Flash/RAM thực xuất hiện ở cuối log build (`RAM:`, `Flash:` percentages) và trong output của `check_firmware_size.py`.

### Chạy native test suite (nhanh hơn build firmware nhiều, không cần thiết bị)

Máy này **không có `cmake` hệ thống**, nhưng PlatformIO có bundle sẵn:

```sh
export PATH="$HOME/.platformio/penv/bin:$HOME/.platformio/packages/tool-cmake/bin:$PATH"
cd /home/vutq/project/xteink-pokemon-game/test
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release   # lần đầu sẽ fetch googletest, hơi lâu
cmake --build build -j4                          # build toàn bộ target (hoặc --target <Tên> cho 1 cái)
cd build && ctest -R "Pokemon" --output-on-failure
```

`test/build/` đã có trong `.gitignore` (`build` — dòng 11).
