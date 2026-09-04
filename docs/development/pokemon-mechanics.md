---
title: Pokémon Module Mechanics
parent: Development
nav_order: 4
---

# Pokémon Module Mechanics

Ghi chú kỹ thuật về cách module Pokémon (`CROSSINK_ENABLE_POKEMON`) hoạt động bên trong CrossInk, dựa trên việc đọc trực tiếp mã nguồn và build thật trên môi trường `pokemon-x3`. Tài liệu này bổ sung chi tiết vận hành cho [docs/pokemon-game.md](../pokemon-game.md) (vốn là tài liệu hướng người dùng cuối).

## 1. Kiến trúc phân lớp

```
EpubReaderActivity (lượt lật trang thật, render e-ink)
        │
PokemonTracker / PokemonTurnVerifier   — đo "thời gian đọc chủ động" thật
        │
PokemonService                          — nghiệp vụ thiết bị: RNG, gọi PokemonGame, gọi PokemonStore
        │
PokemonGame.cpp (lib/Pokemon)           — luật chơi thuần hàm: XP, encounter, item, evolution
        │
PokemonStore + PokemonStoreCodec        — lưu file A/B trên SD, CRC32, versioning
```

- `lib/Pokemon/*` — core logic không phụ thuộc phần cứng, dễ test độc lập (build environment riêng, không cần thiết bị).
- `src/pokemon/*` — lớp tích hợp thiết bị thật (đọc/ghi SD qua `HalStorage`, RNG thật, singleton `devicePokemonService()`).
- `src/activities/pokemon/PokemonActivity.cpp` — UI (Party, PC, Pokédex, resolve encounter/evolution).
- `src/components/pokemon/PokemonArt*.cpp` — đường dẫn & load ảnh Pokémon (BMP) **từ thẻ SD**, không đóng gói vào firmware.

Toàn bộ module được biên dịch có điều kiện qua `#if defined(CROSSINK_ENABLE_POKEMON)` — build môi trường mặc định (`default`) không chứa dòng code nào của module này.

## 2. Đo thời gian đọc "thật" — chống gian lận

File: [lib/Pokemon/PokemonTracker.cpp](../../lib/Pokemon/PokemonTracker.cpp)

- `PokemonTurnVerifier`: cầu nối giữa task xử lý input và task render. `request()` được gọi khi người dùng bấm lật trang; chỉ khi khung hình đã **render xong thành công** trên e-ink thì `renderSucceeded()` mới xác nhận lượt lật hợp lệ (tránh đếm nhầm khi render lag/fail).
- `PokemonTracker`: chỉ cộng dồn giây vào `creditedSeconds_` nếu có một lượt lật trang **thành công** trong `ACTIVE_WINDOW_SECONDS = 300` giây (5 phút) gần nhất.
  - Mở sách không lật trang → không tính giờ.
  - Auto-page-turn (`source == "auto"` trong `EpubReaderActivity`) → bị lọc, không tính.
- Mỗi `CHECKPOINT_MINUTES = 5` phút, `commitWholeMinutes()` chốt số phút nguyên đã tích lũy và gọi `creditMinutes()` xuống `PokemonService` để ghi vào state.
- `flushOnExit()` được gọi khi thoát reader (`EpubReaderActivity.cpp:2125`) để chốt nốt phần phút lẻ (<5 phút) còn lại — nếu thoát app đàng hoàng thì **không mất phút nào**.
- `beginSession()` (gọi khi mở lại reader, `EpubReaderActivity.cpp:1996`) khôi phục phần giây chưa kịp commit từ phiên trước đó **nếu tracker vẫn còn sống trong RAM** — không hoạt động qua một lần khởi động lại thiết bị.

## 3. Vòng lặp game chính — `applyCreditedMinutes()`

File: [lib/Pokemon/PokemonGame.cpp:315](../../lib/Pokemon/PokemonGame.cpp)

Xử lý **từng phút một** (không gộp) để mọi sự kiện random rơi đúng mốc thời gian thật.

**XP & Level**
- Mỗi phút, Pokémon dẫn đầu Party (`leader`) +1 XP, tối đa `MAXIMUM_TOTAL_XP = 8340` (level 100).
- `xpRequired(level) = 10*n + 3*n²/4` với `n = level - 1`.
- `levelForXp()` dùng binary search để tra ngược level từ tổng XP.
- **XP hoàn toàn theo thời gian đọc thật, không phụ thuộc độ dài sách/số chương.**

**Wild Encounter**
- Roll mỗi 15 phút (`ENCOUNTER_CHECK_MINUTES`) hoặc mỗi giờ, xác suất **2/5**.
- Cơ chế pity: trượt 3 lần liên tiếp (`ENCOUNTER_MISSES_BEFORE_GUARANTEE = 3`) → lần tiếp theo chắc chắn trúng.
- Loài được chọn theo trọng số dựa trên `captureRate` gốc; starter (Bulbasaur/Charmander/Squirtle/Pikachu) bị ép trọng số thấp nhất.
- **Gating theo `bookProgressPercent`** (% tiến độ *cuốn sách đang đọc*, không phải tổng số trang/phút đã đọc):
  - Middle-stage evolution chỉ xuất hiện khi `≥50%`.
  - Final-stage chỉ khi `≥75%`.
  - Level band của Pokémon hoang dã cũng tăng theo band này (`0%→lv2-6` ... `95%→lv18-30`).
  - ⚠️ Hệ quả: đọc nhiều sách ngắn (dễ đạt `%` cao) cho encounter chất lượng tốt hơn nhiều so với một cuốn dài, dù tổng thời gian đọc bằng nhau — vì `%` không phải hàm của tổng thời gian mà là hàm của tiến độ trong cuốn hiện tại.
- Legendary (144-146 chim, 150 Mewtwo): cần cả `lifetimeMinutes` tích lũy toàn thời gian **và** `bookProgressPercent` của sách hiện tại đủ ngưỡng.
- Mew (151): chỉ xuất hiện khi đã bắt đủ 150 loài còn lại.
- Giới tính random theo `genderRate` thật của species.

**Item hiếm**
- Roll mỗi giờ, xác suất 1/20, pity sau 19 lần trượt.
- Ưu tiên loại item mà Party đang cần để tiến hóa.

**Evolution**
- Khi lên level, kiểm tra `evolutionsFor(speciesId)` (dữ liệu tĩnh `PokemonSpecies.cpp`); nếu đạt `minimumLevel` → đẩy vào hàng đợi sự kiện chờ xác nhận (trừ khi `RecordFlag::EvolutionPromptsDisabled`).
- Tiến hóa bằng item (`useEvolutionItem`) xử lý ngay, không qua vòng lặp phút.

## 4. Hàng đợi sự kiện (Pending Events)

`PokemonState.pendingEvents`: FIFO tối đa `PENDING_EVENT_CAPACITY = 3`. Khi đầy, encounter/item mới bị bỏ qua nhưng bộ đếm pity (`encounterMisses`/`itemMisses`) vẫn giữ nguyên. `DashboardNotice` hiển thị icon `!` khi có sự kiện chờ.

## 5. Lưu trữ — double-buffer + CRC

File: [src/pokemon/PokemonStore.cpp](../../src/pokemon/PokemonStore.cpp)

- 2 file luân phiên `/.crosspoint/pokemon-a.bin` / `pokemon-b.bin`. Mỗi `commit()` ghi vào file **không active**, rồi swap — mất điện giữa chừng vẫn giữ được bản cũ nguyên vẹn.
- Snapshot = header (version, sequence, recordCount) + state + N record, toàn bộ CRC32.
- `inspectSnapshot()` lúc boot verify: CRC, `sequence` khớp, `recordId` tăng dần liên tục, mọi record trong Party/pending-evolution đều tồn tại.
- `validateRecord()`/`validateState()` (PokemonTypes.cpp) là bất biến được kiểm tra ở **mọi** điểm ghi (candidate → validate → commit) — phòng thủ chống corrupt khi rút điện giữa chừng.
- Bản `pokemon-v2-a/b.bin` cũ tự động migrate sang format hiện tại.

## 6. Ngân sách Flash/RAM thực đo (X3 = ESP32-C3, không PSRAM)

Đo bằng `pio run -e pokemon-x3` / `pio run -e default` (16MB flash, dual-OTA `partitions.csv`: mỗi slot app 6.25MB = 6,553,600 bytes):

| Build | Flash dùng | % | Free trong OTA slot |
|---|---|---|---|
| `default` (không Pokémon) | 6,259,237 B | 95.5% | 280,208 B |
| `pokemon-x3` (có Pokémon) | 6,303,483 B | 96.2% | 235,968 B |

- **Module Pokémon hiện tại chỉ tốn ~43KB flash** — vì artwork nằm trên SD card (`Storage.openFileForRead`, path `/pokemon/sprites/%03u.bmp`...), không đóng gói vào firmware image.
- RAM tĩnh (`.data+.bss`) chỉ dùng **17.7%** (`57,852 / 327,680 bytes`) — không phải nút thắt.
- **Nút thắt thật sự là Flash**: base CrossInk (chưa Pokémon) đã chiếm 95.5% slot OTA. Dư địa còn lại cho tính năng mới chỉ ~230KB.
- `scripts/check_firmware_size.py` (post-build script trong `platformio.ini`) tự động fail build nếu `firmware.bin` vượt partition "app" nhỏ nhất trong `partitions.csv` — đây là cách kiểm tra flash overflow chính thức, chạy mỗi lần `pio run`.
- Tăng dung lượng khả dụng cần đổi partition scheme (bỏ dual-OTA rollback an toàn) — đánh đổi lớn, không nên làm tùy tiện.

## 7. Gợi ý khi mở rộng thêm cơ chế (ví dụ hệ thống trận đấu)

- Bản đồ họa/animation kiểu Pokémon Red gốc: rủi ro cao, dễ vượt 230KB còn lại nếu thêm move data + type chart + animation engine + UI trận đấu mới.
- Bản **text-based** (menu FIGHT/ITEM/PKMN/RUN + log text) khả thi hơn nhiều: tái dùng `EpdFont`/`GfxRenderer`/`ButtonNavigator` đã có, không cần animation engine mới. Ước lượng chi phí thêm ~20-40KB flash — lọt trong dư địa hiện có.
- Điểm cần cẩn thận: chuỗi text đa ngôn ngữ (i18n) cho tên chiêu/log trận đấu dễ phình nếu nhân theo nhiều ngôn ngữ ngay từ đầu.
