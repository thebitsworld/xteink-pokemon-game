# Việc tồn đọng — Pokémon module

Ghi lại để khỏi phải hỏi lại. Nguồn đầy đủ, chi tiết (file:line, cách fix đề
xuất) nằm trong các file audit:
- [docs/development/pokemon-gen1-audit-round2.md](docs/development/pokemon-gen1-audit-round2.md)
- [docs/development/pokemon-gen1-audit-round3.md](docs/development/pokemon-gen1-audit-round3.md)
- [docs/development/pokemon-gen1-audit-round4.md](docs/development/pokemon-gen1-audit-round4.md) (nội dung của nó đã được xử lý xong)
- [docs/development/pokemon-gen1-audit-round5.md](docs/development/pokemon-gen1-audit-round5.md) (re-verify round 4, tìm bug mới 3.1 — đã fix `v0.21.1`)
- [docs/development/pokemon-gen1-audit-round6.md](docs/development/pokemon-gen1-audit-round6.md) (7 bug mới — 5 đã fix `v0.21.2`, xem mục "Round 6" bên dưới)
- [docs/development/pokemon-gen1-audit-round7.md](docs/development/pokemon-gen1-audit-round7.md) (bug lớn 1.1 đã fix `v0.21.2`; tính năng pity-counter đã làm `v0.22.0`; lifetime-summary vẫn chưa làm)

File này chỉ là **danh sách rút gọn** để chọn việc tiếp theo. Xoá/cập nhật dòng
nào đã xử lý xong (kèm version/commit khi merge).

---

## Tối ưu flash — CHỈ LÀM KHI THIẾU BỘ NHỚ (ghi lại 2026-09-20, tại `v0.30.0`)

**Hiện trạng đo thật** (`pio run -e pokemon-x3` ở tag `v0.30.0`): Flash 96,2%
(6.304.211 / 6.553.600 B), ảnh OTA còn trống **235.264 B**; RAM 22,5%. X4 Pro
thường còn dư hơn X3 khoảng 40-50 KB. Chưa cần cắt gì; xử lý khi còn trống quá ít.

**Phân bổ (từ `nm -S` trên `.pio/build/pokemon-x3/firmware.elf`):** font tích hợp
1,47 MB (bitmap + bảng glyph; 39 file font Bitter/Lexend Deca/Inter), chuỗi giao
diện 529 KB (28 ngôn ngữ, mỗi ngôn ngữ 17-31 KB), bảng ngắt từ 350 KB (Đức 206 KB,
Nga 33, Anh 27, Thụy Điển 24, Ukraina 21, Ba Lan 16), trang web tải lên 168 KB
(FilesPageHtml 63 KB, jszip 28 KB), dữ liệu Pokémon 132 KB (đã nhỏ, không đụng).
Phần còn lại ~3,6 MB là mã và thư viện nền (mạng, TLS, EPUB, ESP-IDF).

**Các cách cắt giảm, theo thứ tự nên thử (mức tiết kiệm là ước lượng, cần build đo lại):**
1. **Bảng ngắt từ tiếng Đức (~200 KB):** bỏ hoặc chuyển sang thẻ SD; ảnh hưởng
   người dùng Việt gần như không có. (Mã CrossInk gốc → lệch nhánh gốc.) Nga /
   Thụy Điển / Ukraina / Ba Lan cũng tương tự, 16-33 KB mỗi cái.
2. **Log gỡ lỗi (~50-150 KB, chưa đo):** `ENABLE_SERIAL_LOG` đang bật cả ở bản phát
   hành, 2.084 chỗ ghi log. Tắt hoặc hạ `LOG_LEVEL` ở bản phát hành; đổi lại khó
   chẩn đoán lỗi trên máy thật.
3. **Font tích hợp (200-500 KB):** bỏ các biến thể ít dùng (đậm-nghiêng, cỡ 14/16)
   hoặc cả một họ font. Người dùng vẫn tải được font SD. Cần chọn họ font giữ lại.
4. **Trang web tải lên (60-90 KB):** nén sẵn hoặc chuyển ra thẻ SD (mã gốc).
5. **Tuỳ chọn biên dịch (vài %):** thử LTO / kiểm tra `-Os`; làm trên branch riêng,
   rủi ro lỗi khó gỡ trên ESP32.
6. **Gỡ bớt ngôn ngữ giao diện (17-25 KB mỗi ngôn ngữ, xếp cuối):** chữ Cyrillic,
   Hebrew, Ả Rập tốn nhất (2 byte mỗi ký tự). Đã từng thử gỡ Nga/Ukraina/Belarus/
   Kazakh/Do Thái/Ả Rập: tiết kiệm chưa đáng kể so với công sức nên user quyết định
   không giữ thay đổi đó.

**Giới hạn cần nhớ khi thêm bản dịch:** `scripts/gen_i18n.py` giới hạn mỗi ngôn ngữ
**32.767 byte** dữ liệu chữ. Tiếng Nga/Ukraina/Belarus/Kazakh đã sát giới hạn
(~32,3 KB), tiếng Ả Rập ~31,6 KB. Muốn dịch đầy đủ các ngôn ngữ đó phải đổi định
dạng offset của bộ tạo chuỗi (thêm ~14 KB flash).

**Bản dịch 26 ngôn ngữ đã bị bỏ (2026-09-20):** từng dịch ~5.800 chuỗi CrossInk còn thiếu
cho 26 ngôn ngữ ngoài tiếng Việt (tăng ~150 KB flash, X3 lên ~98,4%), nhưng user quyết
định không giữ; branch đã xoá. Các chuỗi đó vẫn hiện tiếng Anh ở ngôn ngữ ngoài tiếng
Việt và Pokémon/Slideshow. Nếu cần dịch lại sau này, phải cắt giảm bộ nhớ trước.

---

## Round 8 (2026-09-19, tại `v0.28.0`) — rà soát toàn code, user đã triage

**Đã fix (`v0.28.2`, trên branch, chưa merge vào `main`):**
- **A. Đọc TXT/XTC không được cộng điểm Pokémon.** Đã thêm đúng 4 hook
  (`beginReadingSession` ở `onEnter`, `checkpointIfDue` ở `loop`,
  `setBookProgressPercent`+`onSuccessfulPageTurn` sau mỗi lần đổi trang thật,
  `flushOnExit` ở `onExit`) vào cả `TxtReaderActivity.cpp` và
  `XtcReaderActivity.cpp`, bọc `#if defined(CROSSINK_ENABLE_POKEMON)`, đúng
  mẫu EPUB (2 reader này không có khái niệm "auto page-turn" nên không cần lọc
  `source == "auto"`). **Chưa có test hồi quy tự động** - `TxtReaderActivity`/
  `XtcReaderActivity` cần toàn bộ framework Arduino/ESP32 (`HalStorage`,
  `GfxRenderer`...) nên không build được trong native test suite; đã build
  thực tế qua `pio run -e pokemon-x3` để xác nhận biên dịch thay vì unit test
  (xem ghi chú build bên dưới). Cùng khoảng trống như session sửa EPUB gốc đã
  từng ghi nhận và để lại chưa làm.
- **C. Áp tác dụng trước khi trừ item.** Thêm `PokemonService::
  teachMoveAndConsumeItem()`/`useConsumableAndConsumeItem()` (kiểm tra không
  ghi gì → trừ item → áp dụng → hoàn item nếu ghi lỗi, đúng mẫu
  `usePpUp`/`useVitamin`; `teachMove()` tách phần kiểm tra ra
  `resolveTeachTarget()` dùng chung, `useConsumable()` tách thành
  `useConsumableImpl(dryRun)` dùng chung) và cập nhật 4 điểm gọi trong
  `PokemonActivity.cpp` (TM/HM trực tiếp, `TmReplaceSlot`, Medicine thường,
  Medicine giữa trận). Thêm 2 test mới trong `PokemonServiceTest.cpp`. 24/24
  test Pokemon native pass.
  - **Lưu ý build**: `pio run -e pokemon-x3` không chạy được hết trong sandbox
    phiên này - bước bootstrap Python deps của platform `pioarduino/
    platform-espressif32` tải `platformio-core` từ một zip archive trên
    GitHub, bị proxy egress của môi trường chặn 403 (chính sách tổ chức, không
    phải bug code) - **cần người dùng tự chạy `pio run -e pokemon-x3` trước
    khi merge**, đúng quy ước cũ ("Standing conventions") là mọi thay đổi
    chạm code cần build thật trước khi merge vào `main`.

**Hoãn (tạm thời không fix):**
- **F. Slideshow nhẹ:** thư mục chỉ có 1 ảnh vẫn vẽ lại mỗi chu kỳ; danh sách
  ảnh không có giới hạn số lượng.

**Đã quyết định giữ nguyên (không phải bug):**
- **B.** Merge PR #2 (`e051b975`) hoàn nguyên các sửa Slideshow X3 của agent
  khác (vd. hướng xoay theo từng ảnh `orientationForImage`) — chủ đích, vì các
  fix trước đó không hiệu quả.
- **D.** `healPartyOnRead` hồi cả Pokémon đã ngất khi đọc sách — giữ, vì Revive
  khá hiếm.
- **E.** Mất phần lẻ dưới 1 phút khi checkpoint và thời gian trước lần lật
  trang đầu tiên — bỏ qua.

---

## Ý tưởng cải tiến/tính năng mới (2026-09-18) — mục 1, 2, 4 ĐÃ LÀM, mục 3 ĐÃ CODE XONG (`v0.29.0`, chưa merge)

Từ một đợt rà soát toàn bộ tính năng hiện có so với các bản Pokémon gốc (agent
nghiên cứu, không sửa code), xếp theo độ phù hợp với giới hạn của bản mod này
(không multiplayer/wifi, flash hạn chế, core loop dựa thời gian đọc sách). Giữ
lại đây để tham khảo khi có thời gian, **chưa quyết định làm cái nào**.

1. ~~**Màn hình tổng kết thành tích kiểu "Trainer Card"**~~ — ĐÃ LÀM (`v0.27.0`) — trùng với mục 2.2
   round 7 bên dưới (lifetime reading minutes, % Pokédex, số badge...). Dữ
   liệu đã có sẵn hết trong save, chỉ cần 1 màn hình hiển thị, không cần đổi
   save format. Rẻ nhất, "shovel-ready" nhất trong 4 ý tưởng.
2. ~~**Thưởng/ghi nhận khi hoàn thành Pokédex 150/151**~~ — ĐÃ LÀM (`v0.27.0`, dấu ★ trên Trainer Card) — hiện tại phần thưởng
   duy nhất khi bắt đủ 150 loài (chưa tính Mew) là *mở khóa Mew làm encounter
   hoang dã* (`mewIsReady()`, `lib/Pokemon/PokemonGame.cpp:239-245`), không
   có banner/item/danh hiệu nào khác đánh dấu thành tích này. Có thể gộp
   thành 1 banner trong màn hình ở mục 1 thay vì làm hệ thống riêng.
3. **Hall of Fame** — ĐÃ CODE XONG (`v0.29.0`, trên branch, chưa merge vào
   `main`), theo đúng thiết kế đã chốt 2026-09-19 (phương án lưu lại xem sau):
   - `lib/Pokemon/PokemonHallOfFameCodec.h/.cpp` +
     `src/pokemon/PokemonHallOfFameStore.h/.cpp`: side-file mới
     `pokemon-hof-{a,b}.bin`, double-buffered đúng mẫu IV/EV/battle-store
     nhưng đơn giản hơn (1 snapshot cố định, không phải collection theo
     recordId) - header 9 byte + payload cố định (cờ `cleared` + số phút đọc
     lúc thắng + 6 slot `HallOfFameMember`) + CRC32. `cleared` nằm trong
     payload thay vì suy ra từ việc file có tồn tại hay không, để `reset()`
     dùng chung đúng 1 cơ chế ghi (write-verify-flip) thay vì phải xoá file
     riêng.
   - `PokemonService::captureHallOfFame()`: chỉ ghi 1 lần duy nhất (từ chối
     ghi đè nếu đã có), snapshot species/nickname/level/gender/shiny của
     từng slot party hiện tại + `lifetimeMinutes`. `peekHallOfFame()` đọc,
     không bao giờ ghi. `reset()` cũng dọn side-file này.
   - `PokemonActivity.cpp`: `finishGymChallenge()` gọi `captureHallOfFame()`
     ngay khi thắng Champion (`gymIndex == CHAMPION_GYM_INDEX`), rồi hiện màn
     Hall of Fame 1 lần ngay sau message thắng. Xem lại từ **Trainer Card**
     (chạm ô Champion - viền nhấn thêm khi đã mở khoá, hoặc Confirm khi dùng
     nút vì màn Trainer Card không có ô nào để chọn). Không thêm nút thứ 9
     vào menu chính. Màn hình: lưới 2 cột x 3 hàng dùng lại
     `pokemonTrainerCardGrid()` (hàm này được thêm tham số `maxTileHeight`
     tuỳ chọn, mặc định giữ nguyên 72px cho Trainer Card - Hall of Fame
     truyền 150px vì mỗi ô có sprite hero 120x90 + 2 dòng chữ, không chỉ 1
     icon). Thiếu ảnh thì hiện tên, đúng như Trainer Card.
   - Test mới: `PokemonHallOfFameCodecTest`/`PokemonHallOfFameStoreTest`
     (round-trip, validate, double-buffer, capture-once, reset) +
     2 test `PokemonServiceTest` cho `captureHallOfFame`/`peekHallOfFame`.
     26/26 test Pokemon native pass.
   - **Chưa verify được trên `pio run -e pokemon-x3` hay simulator** - cùng
     giới hạn mạng sandbox đã ghi ở mục A/C phía trên (bootstrap PlatformIO
     bị proxy egress chặn 403). `PokemonActivity.cpp` không nằm trong native
     test suite (cần toàn bộ framework Arduino/ESP32) nên phần UI (layout
     lưới, chạm ô Champion, luồng message → Hall of Fame) mới chỉ được review
     kỹ bằng tay, **chưa click-test thật** - bắt buộc phải làm trước khi
     merge vào `main`, đúng quy ước cũ.
4. ~~**Vitamin tăng EV trực tiếp** (HP Up/Protein/Iron/Calcium/Carbos)~~ — ĐÃ LÀM (`v0.28.0`: +10 EV/lần, dùng đến 100 EV/chỉ số, chỉ rơi từ track đọc sách; save v7) — hiện
   EV chỉ tăng qua thắng battle (`awardBattleXp`'s EV yield), chưa có item
   nào tăng EV trực tiếp. Kỹ thuật rẻ (giống hệt pattern PP Up đã có: item id
   mới + công thức cộng có giới hạn + gắn vào drop pool Medicine có sẵn),
   nhưng làm loãng triết lý "EV chỉ tăng gián tiếp qua đọc sách → battle" nếu
   không cẩn thận định vị lại (ví dụ: vẫn chỉ rơi ra từ track đọc sách, không
   mua được). Độ ưu tiên thấp hơn 3 mục trên.

---

## Round 7 (2026-09-15) — bug 1.1 ĐÃ FIX (`v0.21.2`), tính năng pity-counter ĐÃ LÀM (`v0.22.0`)

- [x] **1.1 — ĐÃ FIX (`v0.21.2`)**: `useEvolutionItem()` (`lib/Pokemon/PokemonGame.cpp`)
  bị chặn bởi BẤT KỲ pending event nào, không chỉ event liên quan đến chính
  Pokémon đang evolve. Fix: scope lại đúng theo `record.recordId`, giống
  `resolveEvolution()`.
- [x] **2.1 — ĐÃ LÀM (`v0.22.0`)**: Thêm khối "Coming Up" dưới grid ở
  `Screen::Menu`, hiển thị 5 thanh progress bar pity-counter (Encounter/
  Balls/Medicine/TM-HM/Evolution) — căn giữa màn hình, cách nhau 1 dòng
  trắng, tự bỏ qua nếu không đủ chỗ. Không làm phần lifetimeMinutes/tier
  tiến độ sách (thấy dòng đó gây khó hiểu khi thảo luận, đã bỏ khỏi thiết
  kế cuối).
- [x] **2.2 — ĐÃ LÀM (`v0.27.0`)**: Màn hình Trainer Card thay thế màn Badges
  (giờ đọc, seen/caught/151 + %, lưới 13 icon huy hiệu/Elite Four/Champion, ★
  khi caught ≥ 150).

## Round 6 (2026-09-15, tại `v0.21.1`) — 5/7 bug ĐÃ FIX (`v0.21.2`), 2 mục giữ nguyên theo quyết định user

- [ ] **2.1 — GIỮ NGUYÊN (quyết định của user)** — Tỷ lệ tự đánh trúng mình
  khi Confusion vẫn dùng 33% (`CONFUSION_SELF_HIT_CHANCE_PERCENT`,
  `PokemonBattle.cpp:13`), dù Gen 1 thật là 50% — đã hỏi, user chọn giữ
  nguyên (nhẹ nhàng hơn cho người chơi). Đã thêm comment trong code xác nhận
  đây là lựa chọn có chủ đích.
- [ ] **2.2 — GIỮ NGUYÊN (quyết định của user)** — Paralysis vẫn giảm Speed
  còn 1/2 (`PokemonBattle.cpp:358`), dù Gen 1 thật giảm còn 1/4 — đã hỏi,
  user chọn giữ nguyên. Đã thêm comment trong code xác nhận.
- [x] **2.3 — ĐÃ FIX (`v0.21.2`)** — Substitute giờ chặn đúng flinch/secondary
  stat-drop/status chính trên đòn phá vỡ nó, dùng chung 1 snapshot pre-hit
  (`defenderHadSubstituteAtStart`) ở đầu `resolveGenericMoveEffect()`.
- [x] **2.4 — ĐÃ FIX (`v0.21.2`)** — Trap giờ giải phóng nạn nhân ngay khi bên
  gài trap ngất (`faintCombatant()` nhận thêm tham số `other`) hoặc bị đổi ra
  (`setupBattlePlayer()`/`setupBattleOpponent()` check trước khi reset).
- [x] **2.5 — ĐÃ FIX (`v0.21.2`)** — Reflect/Light Screen/Mist giờ sống sót
  qua switch trong cùng trận (tham số `preserveSideEffects`, true ở 3 điểm
  tiếp diễn cùng trận: đổi Pokémon tự nguyện, AI đổi, gym team tiếp theo;
  false ở các điểm bắt đầu trận mới). Guard Spec./Dire Hit/Focus Energy vẫn
  đúng per-Pokémon như cũ, không đổi.
- [x] **2.6 — ĐÃ FIX (`v0.21.2`)** — Đổi tên Pokémon đã sở hữu giờ quay lại
  đúng `Screen::Actions` thay vì luôn về Menu (`openNickname()` thêm tham số
  `routeSuccessThroughPendingEvent`, mặc định `true` giữ nguyên hành vi
  starter/catch, Rename truyền `false`).
- [x] **2.7 — ĐÃ FIX (`v0.21.2`)** — Ném Ball thất bại giờ gọi
  `finishItemUseMidBattle()` (dùng lại y hệt cơ chế item/switch), cho đối thủ
  ra đòn đúng luật.
- [x] **2.8 — ĐÃ FIX (`v0.27.1`)** — Bide release giờ kích hoạt Rage (đòn của
  đối thủ). Confusion self-hit cố ý KHÔNG kích hoạt: Rage chỉ phản ứng với đòn
  của đối thủ (định nghĩa trong `BattleCombatant::enraged`).
- [x] **2.9 — ĐÃ FIX (`v0.27.1`, `PokemonService::usePpUp()` kiểm tra → tiêu item → áp dụng, hoàn lại item nếu ghi lỗi)** — PP Up áp dụng +
  lưu trước khi xác nhận tiêu item thành công — cùng dạng bug round 3 đã fix
  cho battle-boost item nhưng chưa áp dụng cho đường PP Up.

## Round 5 (2026-09-15, `v0.21.1`)

Audit vòng 5 re-verify toàn bộ 18 mục "đã xong" của round 4 (tất cả đúng, không
regression) và trả lời 2 câu hỏi mở còn treo (công thức run-away `+30×số lần
thử` — đúng theo Gen 1 thật; `movePriority()` hand-authored — đúng lựa chọn,
khớp convention có sẵn). Phát hiện 1 bug mới:

- [x] **3.1 (round 5) — ĐÃ FIX**: `toxicCounter` (field mới của Toxic tăng
  dần, round 4) không được lưu vào `BattleRecordEntry` — mỗi lần đổi Pokémon
  giữa trận, damage Toxic âm thầm tụt về 1/8 cố định. Fix: bump format
  `pokemon-battle-{a,b}.bin` lên v3 (21 byte/entry, theo đúng tiền lệ PP Up
  v1→v2), thêm `toxicCounter` vào 3 điểm đọc/ghi
  (`savePlayerBattleEntry()`, `setupBattlePlayer()`, item-sync mid-battle) +
  2 điểm cure-status trong `PokemonService.cpp` (đảm bảo invariant
  `toxicCounter` chỉ khác 0 khi `status == Poison`).

## Trạng thái hiện tại (cập nhật 2026-09-15, `v0.21.0`)

**Đã xong toàn bộ** trong đợt này (bug 2.9, tất cả 8 mục hiệu năng 3.1–3.8, và
7/8 tính năng+design-call round 2/3 — commit `009f2345`, `66b2e7fc`,
`19d0a28b`/`ae5b2430`, merge `214bd9ca`/`d2db0720`/`adf28780`, changelog
`8fd4b57b`). 24/24 native test pass, `pio run -e pokemon-x3` build thành công
(Flash 94.2%, RAM 22.4%) trên `main` sau merge.

## 1. Bug — ĐÃ XONG

- [x] **2.9** — Mutual-KO tự gây ra giờ báo "Pokémon của bạn cũng ngất" + ép đổi
  Pokémon trước khi tiếp tục Gym/Elite Four/Champion.
- [x] *(ĐÃ DỌN `v0.27.1`: 1 hàm `catchBlockedByFullBox()` dùng chung cho service và UI)* fix bug 2.6
  (Box-full gate trước khi ném Ball) dùng 1 gate trùng lặp riêng thay vì sửa
  tận gốc thứ tự gọi hàm gốc — nếu sau này 2 công thức lệch nhau, lỗi cũ có
  thể quay lại. Cân nhắc dọn lại khi có dịp đụng tới `Screen::BattleBalls`.

## 2. Hiệu năng — ĐÃ XONG (tất cả 3.1–3.8)

- [x] **3.1** — `PokemonStore::readRecords()` mới, quét 1 lượt cho cả party
  thay vì 6 lần `readRecord()` riêng.
- [x] **3.2** — `healPartyOnRead()` phần đọc dùng chung `readRecords()`.
- [x] **3.3** — Sắp xếp PC Box giờ 1 lượt quét duy nhất (đếm + xếp vị trí theo
  loài), không re-scan theo từng loài nữa.
- [x] **3.4** — Buffer ghi IV/EV (`IvEvStoreWriteFileBytes`, 518 entry) tách
  riêng khỏi buffer decode file cũ (vẫn giữ 1024 cho tương thích ngược).
- [x] **3.5** — Write-verify IV/EV giờ so byte trực tiếp (`memcmp`), không
  decode lại state nữa.
- [x] **3.6** — `BatchedRecordReader` nhận `chunkRecordCapacity` tuỳ chỉnh;
  `readRecord()` (tra 1 id) dùng chunk nhỏ thay vì luôn cấp ~2KB.
- [x] **3.7** — Summary screen đọc thẳng `focusedRecord_`, không gọi lại
  `service_.readRecord()` mỗi frame.
- [x] **3.8** — `refreshSnapshot()` tìm trong `snapshot_.party[]` (RAM) trước
  khi fallback đọc SD.

## 3. Tính năng thiếu so với Gen 1 — ĐÃ XONG 1.1–1.7, CHƯA LÀM 1.8

- [x] 1.1 — Miễn nhiễm trạng thái theo hệ (Fire không bị Burn, Poison không bị
  Poison/Toxic, Ice không bị Freeze)
- [x] 1.2 — Thêm 3 chiêu multi-hit: Double Kick (24, luôn 2 hit), Spike Cannon
  (131, phân phối 2/3/4/5 ngẫu nhiên), Bonemerang (155, luôn 2 hit)
- [x] 1.3 — Secondary stat-drop: Acid/Bubble Beam/Aurora Beam/Constrict/Bubble
  10%, Psychic ~33% (quirk gốc Gen 1)
- [x] 1.4 — Razor Wind (13) giờ là chiêu 2 lượt (không có bất tử như Fly/Dig)
- [x] 1.5 — Hyper Beam không bắt recharge nếu đòn đó hạ gục địch
- [x] 1.6 — Jump Kick/Hi Jump Kick gây 1 HP crash damage khi miss
- [x] 1.7 — Teleport: wild battle = thoát chắc chắn (như Run), trainer/gym =
  thất bại ("But it failed!")
- [ ] **1.8 — CHƯA LÀM, theo yêu cầu user (2026-09-15): Repel hiện chưa có ý
  nghĩa rõ ràng trong gameplay của bản mod này, cần suy nghĩ thêm trước khi
  triển khai.** Không tự làm khi chưa có yêu cầu mới.

## 4. Design-call round 2 — ĐÃ QUYẾT ĐỊNH VÀ LÀM XONG 5/8, GIỮ NGUYÊN 3/8

Đã hỏi ý kiến user (2026-09-15) và triển khai xong:

- [x] Move priority — Quick Attack +1 (luôn đi trước), Counter −1 (luôn đi
  sau), là khoá sắp xếp lượt đánh chính, Speed chỉ phá hoà khi cùng priority
- [x] Toxic — damage tăng dần `n × maxHP/16` mỗi lượt (n không giới hạn),
  tách biệt khỏi Poison thường (vẫn 1/8 cố định)
- [x] Chạy trốn (RUN, chỉ áp dụng wild battle) — có thể thất bại theo công
  thức Speed-ratio thật của Gen 1 (+ bonus mỗi lần thử thất bại); Gym/Elite
  Four/Champion vẫn luôn không chạy được (giữ nguyên)
- [x] Haze — giờ reset đầy đủ: status/confusion/Reflect/Light Screen/Mist/
  Focus Energy của cả 2 bên, không chỉ stat stages
- [x] Speed tie — tung xu 50/50 thay vì luôn ưu tiên player

**User chọn GIỮ NGUYÊN** (không đổi, đã ghi chú rõ trong code để audit sau
không hỏi lại):

- [ ] ~~Status/Leech Seed damage 1/8 vs 1/16~~ — giữ 1/8
- [ ] ~~Freeze 20%/lượt tự khỏi vs vĩnh viễn~~ — giữ 20%/lượt
- [ ] ~~Counter phản mọi đòn vật lý vs chỉ Normal/Fighting~~ — giữ phản mọi đòn

**2 quirk "nghi vấn" — đã xác nhận đúng theo Gen 1 gốc, giữ nguyên + comment
giải thích** (không phải bug):

- [x] Substitute hấp thụ damage vẫn feed Counter/Bide/Rage — quirk thật của
  Gen 1, đã ghi chú trong code
- [x] Thức dậy khỏi Sleep không tốn lượt — đúng hành vi Gen 1 gốc, đã ghi chú

---

## Lưu ý cần review thêm (phát sinh trong quá trình triển khai, chưa ai xác nhận)

- **Công thức chạy trốn dùng thêm hệ số `+30×số lần đã thử thất bại`** (theo
  Bulbapedia, không chỉ công thức rút gọn ban đầu) — nên xác nhận đây đúng là
  ý muốn, không phải diễn giải sai.
- **Move priority triển khai bằng hàm `movePriority(moveId)` hand-authored**,
  không thêm field vào `MoveData` (vốn sinh từ CSV) — theo đúng convention có
  sẵn của file (bảng crit/recoil/stat-change cũng hand-authored), nhưng khác
  với đề bài gốc yêu cầu thêm field.

---

## Quy ước làm việc (nhắc lại, đã thống nhất từ trước)

- Không push lên `origin` nếu chưa được đồng ý rõ ràng trong phiên đó.
- Mỗi việc lớn: tạo branch riêng → code → chạy full test suite native
  (`ctest -R Pokemon`) → build firmware (`pio run -e pokemon-x3`) → thêm
  `CHANGELOG.md` + bump version `platformio.ini` → merge `--no-ff` vào `main`
  → xoá branch.
- Việc chỉ mang tính nghiên cứu/audit (không sửa code) thì không cần tạo
  branch/version bump, chỉ commit doc.
- Với design-call: mô tả phương án trước, hỏi ý kiến, rồi mới code.
