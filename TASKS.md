# Việc tồn đọng — Pokémon module

Ghi lại để khỏi phải hỏi lại. Nguồn đầy đủ, chi tiết (file:line, cách fix đề
xuất) nằm trong 3 file audit:
- [docs/development/pokemon-gen1-audit-round2.md](docs/development/pokemon-gen1-audit-round2.md)
- [docs/development/pokemon-gen1-audit-round3.md](docs/development/pokemon-gen1-audit-round3.md)
- [docs/development/pokemon-gen1-audit-round4.md](docs/development/pokemon-gen1-audit-round4.md) (mới nhất, cập nhật 2026-09-15, tại `v0.20.3`)

File này chỉ là **danh sách rút gọn** để chọn việc tiếp theo. Xoá/cập nhật dòng
nào đã xử lý xong (kèm version/commit khi merge).

---

## 1. Bug (round 4)

- [ ] **2.9** — Khi player thắng do mutual-KO (Pokémon của mình cũng ngất theo,
  vd Explosion/Self-Destruct), game không báo "Pokémon fainted!" và không ép
  đổi Pokémon trước trận kế tiếp. UX gap nhỏ, không mất dữ liệu/exploit.
  → `PokemonActivity::resolveBattlePlayerMoveTurn()`, cần check
  `battlePlayer_.currentHp == 0` cùng với `PlayerWon` trước khi vào flow thắng
  thường.
- [ ] *(ghi chú rủi ro, không phải bug đang xảy ra)* fix bug 2.6 (Box-full gate
  trước khi ném Ball) dùng 1 gate trùng lặp riêng thay vì sửa tận gốc thứ tự
  gọi hàm gốc — nếu sau này 2 công thức lệch nhau, lỗi cũ có thể quay lại. Cân
  nhắc dọn lại khi có dịp đụng tới `Screen::BattleBalls`.

## 2. Hiệu năng (round 3 còn mở + round 4 mới)

Khuyến nghị làm **3.1 + 3.8 + 3.7** cùng lúc (liên quan trực tiếp nhau, cùng
đường gọi):

- [ ] **3.1** — `loadSnapshot()` mở/scan file 6 lần riêng biệt (mỗi thành viên
  party 1 lần `readRecord()`). Cần 1 hàm `readRecords()` tra nhiều id trong 1
  lượt quét.
- [ ] **3.8 (mới)** — Bản fix bug 2.2 (`refreshSnapshot()`) vô tình thêm 1 lượt
  `readRecord()` scan SD nữa, lặp lại ở ~30 điểm gọi mutation trong
  `PokemonActivity.cpp`. Fix: tìm trong `snapshot_.party[]` (đã có sẵn trong
  RAM) trước, chỉ fallback đọc SD nếu Pokémon đang ở PC Box. Nên làm chung với
  3.1.
- [ ] **3.7** — Summary screen vẫn tự gọi `service_.readRecord()` mỗi frame dù
  `focusedRecord_` giờ đã đáng tin cậy (sau fix 2.2). Chỉ cần đổi 1 dòng sang
  đọc `focusedRecord_`.
- [ ] **3.2** — `healPartyOnRead()` phần đọc (không phải ghi, ghi đã fix ở 2.7)
  vẫn quét lặp mỗi 5 phút, 1 lần/thành viên party.
- [ ] **3.3** — Sắp xếp PC Box vẫn decode lại toàn bộ record theo từng loài
  riêng biệt — O(species × N).
- [ ] **3.4** — Buffer ghi IV/EV (`IvEvStoreFileBytes`) vẫn theo size cũ 1024
  entry, chưa theo cap mới 518 (từ bug 2.3).
- [ ] **3.5** — Write-verify IV/EV vẫn cấp phát thêm 1 `IvEvStoreState` 8KB để
  so sánh, thay vì so byte trực tiếp sau khi ghi.
- [ ] **3.6** — `BatchedRecordReader` luôn cấp heap ~2KB kể cả khi chỉ tra 1
  record.

## 3. Tính năng thiếu so với Gen 1 (round 3 còn mở + round 4 mới)

- [ ] 1.1 — Miễn nhiễm trạng thái theo hệ (Fire→burn, Poison→poison, Ice→freeze)
- [ ] 1.2 — Thiếu 3 chiêu multi-hit: Double Kick (24), Spike Cannon (131),
  Bonemerang (155)
- [ ] 1.3 — Thiếu secondary stat-drop cho 1 số chiêu damage (Acid, Bubble Beam,
  Aurora Beam, Psychic, Constrict, Bubble)
- [ ] 1.4 — Razor Wind (13) chưa phải chiêu 2 lượt
- [ ] 1.5 — Hyper Beam vẫn bắt recharge kể cả khi hạ gục địch ngay lượt đó
- [ ] 1.6 — Jump Kick/Hi Jump Kick (26/136) không gây crash damage khi miss
- [ ] 1.7 — Teleport (100) không có tác dụng gì trong wild battle
- [ ] **1.8 (mới)** — Chưa có item kiểu Repel để chặn wild encounter tạm thời.
  Đề xuất: thêm field đếm phút đọc còn hiệu lực vào `PokemonState` (giống cách
  `ppUpCount` đã làm), 1 item id mới, gate ở đầu `processEncounterCheck()`.

## 4. Design-call còn treo (round 2, đã nêu lại 3 lần, cần quyết định trước khi làm)

Đây là các mục **cần hỏi ý kiến** trước khi code (đổi hành vi/cân bằng game),
không phải bug rõ ràng:

- [ ] Move priority (Quick Attack +1, Counter −1) — hiện toàn bộ turn order
  chỉ dựa Speed
- [ ] Toxic nên tăng dần damage theo lượt (hiện dùng chung mức cố định với
  Poison thường)
- [ ] Chạy trốn (RUN) hiện luôn thành công 100%, Gen 1 có tỷ lệ thất bại
- [ ] Haze hiện chỉ reset stat stages, Gen 1 còn reset cả status/confusion/
  Reflect/Light Screen/Mist/Focus Energy
- [ ] Status/Leech Seed damage đang dùng 1/8, Gen 1 gốc dùng 1/16
- [ ] Speed tie hiện luôn ưu tiên player, Gen 1 tung xu 50/50
- [ ] Freeze hiện có 20%/lượt tự khỏi, Gen 1 đóng băng vĩnh viễn (chỉ khỏi khi
  bị đánh trúng non-Ice hoặc dùng vài chiêu đặc biệt)
- [ ] Counter hiện phản mọi đòn vật lý, Gen 1 chỉ phản Normal/Fighting
- [ ] *(nghi vấn, chưa xác nhận là bug)* damage bị Substitute hấp thụ vẫn được
  tính vào Counter/Bide/Rage của bên tấn công
- [ ] *(nghi vấn, chưa xác nhận là bug)* thức dậy khỏi Sleep không tốn lượt,
  chiêu vẫn ra ngay lượt đó

---

## Quy ước làm việc (nhắc lại, đã thống nhất từ trước)

- Không push lên `origin` nếu chưa được đồng ý rõ ràng trong phiên đó.
- Mỗi việc lớn: tạo branch riêng → code → chạy full test suite native
  (`ctest -R Pokemon`) → build firmware (`pio run -e pokemon-x3`) → thêm
  `CHANGELOG.md` + bump version `platformio.ini` → merge `--no-ff` vào `main`
  → xoá branch.
- Việc chỉ mang tính nghiên cứu/audit (không sửa code) thì không cần tạo
  branch/version bump, chỉ commit doc.
- Với design-call (mục 4): mô tả phương án trước, hỏi ý kiến, rồi mới code.
