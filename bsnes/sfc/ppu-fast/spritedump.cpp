//sprite dumper (development aid for sprite replacement)
//set the environment variable BSNES_SPRITE_DUMP to an existing directory to enable it.
//every unique OAM sprite is written once as an indexed PNG (<hash>.png) and logged in manifest.csv.
//the hash covers the 4bpp color indices of the unflipped tiles, so palette swaps and flips share one hash.
//hashes already present in manifest.csv are skipped, so the dump accumulates across runs and nothing is deleted.
//each run also writes frames-<date>-<time>.csv: every on-screen sprite per frame (only when the sprite set changes),
//for grouping the pieces of a character. x/y are signed screen coordinates of the top-left corner.
//fully transparent sprites and sprites parked outside the visible area are not logged.
//
//replacement pack (development stage 2: recognition only, nothing is drawn):
//set BSNES_SPRITE_PACK to a directory containing pack.txt (one "id png scale width height" line per group).
//each frame the on-screen sprites are grouped (boxes within 1 pixel of each other are joined) and every group
//gets an id: FNV-1a 64 over "hash:dx:dy:hflip:vflip:width:height;" of its pieces sorted by those fields, with
//dx/dy relative to the group's top-left corner. groups listed in pack.txt are logged to recognized-<date>-<time>.csv
//(frame,id,x,y,width,height), only when the set of recognized groups changes. the dump and the pack are independent.
//
//replacement pack (development stage 3): the png named in pack.txt (an RGBA image of scale * the group's size, scale
//being the 3rd column) is drawn over the recognized group in a frame enlarged by that scale. the enlarged frame is the
//native frame enlarged with nearest neighbour; the pixels of the recognized sprites are first replaced by a second,
//sprite-less render of the same frame (so nothing of the original shows through), then the image is drawn where the
//original sprite was not hidden behind a background layer (hidden pixels plus a 1 pixel margin stay hidden); outside the
//original outline the image is drawn freely. "visible" and "hidden" come from the layer buffers of the normal render (which layer
//won each pixel), not from comparing colors, so fades and transparency do not confuse them; the image's colors go through the same
//window / color math / brightness path as the original sprite (so water tint and fades apply to it too).
//only used for plain 256-wide progressive frames (not hires, interlace or mode 7 HD); software filters are bypassed.

namespace {

struct SpriteDumper {
  bool initialized = false;
  bool enabled = false;      //dump or pack active
  bool dumpEnabled = false;
  bool packEnabled = false;
  string directory;
  static const uint packMax = 1024;
  char packIds[packMax][17] = {};
  uint packCount = 0;
  FILE* recognizedFile = nullptr;
  uint64_t lastRecognizedHash = 0;
  struct Recognized { uint pack; int x, y, width, height; const uint8_t* opaque; } recognized[128];
  static const uint maskPoolSize = 1 << 19;
  uint8_t* maskPool = nullptr;  //opaque pixels of every recognized group (width * height bytes each)
  uint8_t* wonPool = nullptr;   //same layout: pixels where a sprite was the visible layer in the frame
  uint maskPoolUsed = 0;
  uint recognizedCount = 0;
  bool recognizedFresh = false;
  //replacement images (RGBA, scale times the native size) and the buffers used to compose the high-resolution frame
  uint8_t* packImage[packMax] = {};
  uint packImageWidth[packMax] = {};
  uint packImageHeight[packMax] = {};
  uint packPad[packMax] = {};  //native pixels of margin around the group's box in each image
  uint packScale = 0;
  bool packReady = false;
  string packDirectory;
  uint debugCaptured = 0;
  uint leakCaptured = 0;
  FILE* unrecognizedFile = nullptr;
  uint64_t lastUnrecognizedHash = 0;
  uint16_t* hdBuffer = nullptr;
  uint16_t* bgBuffer = nullptr;
  uint64_t seen[1 << 16] = {};  //open-addressing set of sprite hashes (0 = empty)
  uint count = 0;
  uint frame = 0;
  FILE* framesFile = nullptr;
  uint64_t lastFrameHash = 0;
};
SpriteDumper spriteDumper;

struct SpriteEntry {
  uint8_t oam;
  uint64_t hash;
  int x, y;
  uint width, height;
  uint hflip, vflip, priority, palette;
  uint64_t rows[64];  //opaque pixels of the unflipped sprite, one bit per column
};

//joins sprites whose boxes are within 1 pixel of each other and logs the groups present in the pack
auto recognizeGroups(SpriteDumper& dumper, const SpriteEntry* entries, uint count, uint frame) -> void {
  int parent[128];
  for(uint i = 0; i < count; i++) parent[i] = i;
  auto find = [&](int i) { while(parent[i] != i) { parent[i] = parent[parent[i]]; i = parent[i]; } return i; };
  for(uint i = 0; i < count; i++) for(uint j = i + 1; j < count; j++) {
    const SpriteEntry& a = entries[i];
    const SpriteEntry& b = entries[j];
    bool apart = a.x + (int)a.width + 1 < b.x || b.x + (int)b.width + 1 < a.x
              || a.y + (int)a.height + 1 < b.y || b.y + (int)b.height + 1 < a.y;
    if(!apart) parent[find(i)] = find(j);
  }

  dumper.recognizedCount = 0;
  dumper.maskPoolUsed = 0;
  static char sigBuffer[128 * 48 + 256];
  uint sigUsed = 0;
  struct Unrecognized { char id[17]; int x, y, width, height; uint sig; } unrecognized[128];
  uint unrecognizedCount = 0;
  uint64_t unrecognizedHash = 0xcbf29ce484222325ull;
  struct Found { char id[17]; int x, y, width, height; } found[128];
  uint foundCount = 0;
  uint64_t recognizedHash = 0xcbf29ce484222325ull;
  bool done[128] = {};
  for(uint root = 0; root < count; root++) {
    if(done[root]) continue;
    uint members[128];
    uint memberCount = 0;
    int minX = 1 << 20, minY = 1 << 20, maxX = -(1 << 20), maxY = -(1 << 20);
    for(uint i = 0; i < count; i++) {
      if(find(i) != find(root)) continue;
      done[i] = true;
      members[memberCount++] = i;
      const SpriteEntry& e = entries[i];
      if(e.x < minX) minX = e.x;
      if(e.y < minY) minY = e.y;
      if(e.x + (int)e.width  > maxX) maxX = e.x + (int)e.width;
      if(e.y + (int)e.height > maxY) maxY = e.y + (int)e.height;
    }
    //text key of each piece, sorted by (hash text, dx, dy, hflip, vflip, width, height)
    struct Piece { char hash[17]; int dx, dy; uint hflip, vflip, width, height; } pieces[128];
    for(uint k = 0; k < memberCount; k++) {
      const SpriteEntry& e = entries[members[k]];
      Piece& p = pieces[k];
      snprintf(p.hash, sizeof(p.hash), "%016llx", (unsigned long long)e.hash);
      p.dx = e.x - minX; p.dy = e.y - minY;
      p.hflip = e.hflip; p.vflip = e.vflip; p.width = e.width; p.height = e.height;
    }
    auto less = [](const Piece& a, const Piece& b) {
      int c = strcmp(a.hash, b.hash);
      if(c) return c < 0;
      if(a.dx != b.dx) return a.dx < b.dx;
      if(a.dy != b.dy) return a.dy < b.dy;
      if(a.hflip != b.hflip) return a.hflip < b.hflip;
      if(a.vflip != b.vflip) return a.vflip < b.vflip;
      if(a.width != b.width) return a.width < b.width;
      return a.height < b.height;
    };
    for(uint i = 1; i < memberCount; i++) {
      Piece key = pieces[i];
      uint j = i;
      while(j > 0 && less(key, pieces[j - 1])) { pieces[j] = pieces[j - 1]; j--; }
      pieces[j] = key;
    }
    uint64_t id = 0xcbf29ce484222325ull;
    char text[96];
    uint sigStart = sigUsed;
    for(uint k = 0; k < memberCount; k++) {
      const Piece& p = pieces[k];
      int length = snprintf(text, sizeof(text), "%s:%d:%d:%u:%u:%u:%u;", p.hash, p.dx, p.dy, p.hflip, p.vflip, p.width, p.height);
      for(int n = 0; n < length; n++) { id ^= (uint8_t)text[n]; id *= 0x100000001b3ull; }
      memcpy(sigBuffer + sigUsed, text, length);
      sigUsed += length;
    }
    sigBuffer[sigUsed++] = 0;
    char idText[17];
    snprintf(idText, sizeof(idText), "%016llx", (unsigned long long)id);
    bool known = false;
    for(uint n = 0; n < dumper.packCount; n++) {
      if(strcmp(idText, dumper.packIds[n])) continue;
      known = true;
      Found& f = found[foundCount++];
      memcpy(f.id, idText, 17);
      f.x = minX; f.y = minY; f.width = maxX - minX; f.height = maxY - minY;
      //opaque pixels of the group as drawn (flips applied): needed to tell "hidden behind the background" from "transparent"
      uint8_t* opaque = nullptr;
      uint area = (uint)f.width * f.height;
      if(dumper.maskPool && dumper.maskPoolUsed + area <= dumper.maskPoolSize) {
        opaque = dumper.maskPool + dumper.maskPoolUsed;
        dumper.maskPoolUsed += area;
        memset(opaque, 0, area);
        for(uint k = 0; k < memberCount; k++) {
          const SpriteEntry& e = entries[members[k]];
          for(uint ry = 0; ry < e.height; ry++) for(uint rx = 0; rx < e.width; rx++) {
            uint sx = e.hflip ? e.width - 1 - rx : rx;
            uint sy = e.vflip ? e.height - 1 - ry : ry;
            if(e.rows[sy] >> sx & 1) opaque[(e.y - minY + ry) * f.width + (e.x - minX + rx)] = 1;
          }
        }
      }
      dumper.recognized[dumper.recognizedCount++] = {n, f.x, f.y, f.width, f.height, opaque};
      uint64_t fields[] = {id, (uint64_t)(minX + 512), (uint64_t)(minY + 256)};
      for(uint64_t field : fields) { recognizedHash ^= field; recognizedHash *= 0x100000001b3ull; }
      break;
    }
    //character-sized groups that are not in the pack are logged to unrecognized-<date>-<time>.csv
    if(!known && dumper.unrecognizedFile && (memberCount >= 3 || maxY - minY >= 24)) {
      Unrecognized& u = unrecognized[unrecognizedCount++];
      memcpy(u.id, idText, 17);
      u.x = minX; u.y = minY; u.width = maxX - minX; u.height = maxY - minY; u.sig = sigStart;
      uint64_t fields[] = {id, (uint64_t)(minX + 512), (uint64_t)(minY + 256)};
      for(uint64_t field : fields) { unrecognizedHash ^= field; unrecognizedHash *= 0x100000001b3ull; }
    }
  }
  if(!unrecognizedCount) dumper.lastUnrecognizedHash = 0;
  else if(unrecognizedHash != dumper.lastUnrecognizedHash) {
    dumper.lastUnrecognizedHash = unrecognizedHash;
    for(uint i = 0; i < unrecognizedCount; i++)
      fprintf(dumper.unrecognizedFile, "%u,%s,%d,%d,%d,%d,%s\n", frame, unrecognized[i].id, unrecognized[i].x, unrecognized[i].y,
        unrecognized[i].width, unrecognized[i].height, sigBuffer + unrecognized[i].sig);
    fflush(dumper.unrecognizedFile);
  }
  dumper.recognizedFresh = true;
  if(!foundCount) dumper.lastRecognizedHash = 0;
  if(foundCount && recognizedHash != dumper.lastRecognizedHash) {
    dumper.lastRecognizedHash = recognizedHash;
    for(uint i = 0; i < foundCount; i++)
      fprintf(dumper.recognizedFile, "%u,%s,%d,%d,%d,%d\n", frame, found[i].id, found[i].x, found[i].y, found[i].width, found[i].height);
    fflush(dumper.recognizedFile);
  }
}

//returns true if the hash was newly inserted
auto spriteInsert(SpriteDumper& dumper, uint64_t hash) -> bool {
  if(hash == 0) hash = 1;
  if(dumper.count >= (1 << 15)) return false;  //table at half capacity: stop collecting new sprites
  uint mask = (1 << 16) - 1;
  for(uint slot = hash & mask;; slot = slot + 1 & mask) {
    if(dumper.seen[slot] == hash) return false;
    if(dumper.seen[slot] == 0) { dumper.seen[slot] = hash; dumper.count++; return true; }
  }
}

auto spriteCRC32(uint32_t crc, const uint8_t* data, uint size) -> uint32_t {
  for(uint n = 0; n < size; n++) {
    crc ^= data[n];
    for(uint k = 0; k < 8; k++) crc = (crc >> 1) ^ (crc & 1 ? 0xedb88320u : 0u);
  }
  return crc;
}

auto spritePNGChunk(FILE* fp, const char type[4], const uint8_t* data, uint size) -> void {
  uint8_t header[8] = {
    uint8_t(size >> 24), uint8_t(size >> 16), uint8_t(size >> 8), uint8_t(size),
    uint8_t(type[0]), uint8_t(type[1]), uint8_t(type[2]), uint8_t(type[3])
  };
  fwrite(header, 1, 8, fp);
  uint32_t crc = spriteCRC32(0xffffffffu, header + 4, 4);
  if(size) {
    fwrite(data, 1, size, fp);
    crc = spriteCRC32(crc, data, size);
  }
  crc = ~crc;
  uint8_t tail[4] = {uint8_t(crc >> 24), uint8_t(crc >> 16), uint8_t(crc >> 8), uint8_t(crc)};
  fwrite(tail, 1, 4, fp);
}

//8-bit indexed PNG, palette index 0 transparent, zlib "stored" (uncompressed) deflate block
auto spriteWritePNG(const char* path, uint width, uint height, const uint8_t* indices, const uint8_t palette[16][3]) -> bool {
  FILE* fp = fopen(path, "wb");
  if(!fp) return false;

  static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  fwrite(signature, 1, 8, fp);

  uint8_t ihdr[13] = {
    uint8_t(width >> 24), uint8_t(width >> 16), uint8_t(width >> 8), uint8_t(width),
    uint8_t(height >> 24), uint8_t(height >> 16), uint8_t(height >> 8), uint8_t(height),
    8, 3, 0, 0, 0
  };
  spritePNGChunk(fp, "IHDR", ihdr, 13);
  spritePNGChunk(fp, "PLTE", &palette[0][0], 16 * 3);
  const uint8_t trns[1] = {0};
  spritePNGChunk(fp, "tRNS", trns, 1);

  uint rawSize = (width + 1) * height;  //filter byte + pixels per row (always < 65535 for <= 64x64)
  uint8_t raw[(64 + 1) * 64];
  for(uint y = 0; y < height; y++) {
    raw[y * (width + 1)] = 0;
    memcpy(raw + y * (width + 1) + 1, indices + y * width, width);
  }

  uint32_t a = 1, b = 0;
  for(uint n = 0; n < rawSize; n++) {
    a = (a + raw[n]) % 65521;
    b = (b + a) % 65521;
  }
  uint32_t adler = b << 16 | a;

  uint8_t idat[2 + 5 + (64 + 1) * 64 + 4];
  uint size = 0;
  idat[size++] = 0x78; idat[size++] = 0x01;  //zlib header
  idat[size++] = 0x01;                       //final stored block
  idat[size++] = uint8_t(rawSize); idat[size++] = uint8_t(rawSize >> 8);
  idat[size++] = uint8_t(~rawSize); idat[size++] = uint8_t(~rawSize >> 8);
  memcpy(idat + size, raw, rawSize); size += rawSize;
  idat[size++] = uint8_t(adler >> 24); idat[size++] = uint8_t(adler >> 16);
  idat[size++] = uint8_t(adler >> 8);  idat[size++] = uint8_t(adler);
  spritePNGChunk(fp, "IDAT", idat, size);

  spritePNGChunk(fp, "IEND", nullptr, 0);
  fclose(fp);
  return true;
}

}

auto PPU::dumpSprites() -> void {
  auto& dumper = spriteDumper;
  if(!dumper.initialized) {
    dumper.initialized = true;
    if(const char* path = getenv("BSNES_SPRITE_DUMP")) {
      if(*path) {
        dumper.directory = path;
        //the directory must already exist; probe it by opening the manifest for append
        string manifestPath = {dumper.directory, "/manifest.csv"};
        if(FILE* fp = fopen(manifestPath, "ab")) {
          fclose(fp);
          dumper.dumpEnabled = true;
        }
      }
    }
    if(const char* packPath = getenv("BSNES_SPRITE_PACK")) {
      if(*packPath) {
        dumper.packDirectory = packPath;
        string packFile = {packPath, "/pack.txt"};
        if(FILE* fp = fopen(packFile, "rb")) {
          char line[512];
          while(fgets(line, sizeof(line), fp) && dumper.packCount < dumper.packMax) {
            if(strlen(line) >= 16 && line[16] == ' ') {
              uint index = dumper.packCount;
              memcpy(dumper.packIds[index], line, 16);
              dumper.packIds[index][16] = 0;
              dumper.packCount++;
              char fileName[256] = {};
              uint scaleColumn = 0, widthColumn = 0, heightColumn = 0, padColumn = 0;
              if(sscanf(line + 17, "%255s %u %u %u %u", fileName, &scaleColumn, &widthColumn, &heightColumn, &padColumn) >= 4
              && scaleColumn >= 2 && scaleColumn <= 8 && (!dumper.packScale || dumper.packScale == scaleColumn)) {
                Decode::PNG png;
                string imagePath = {packPath, "/", fileName};
                if(png.load(imagePath) && png.info.bitDepth == 8 && (png.info.colorType == 6 || png.info.colorType == 2)
                && png.info.width == widthColumn && png.info.height == heightColumn) {
                  uint pixels = png.info.width * png.info.height;
                  uint8_t* rgba = new uint8_t[pixels * 4];
                  for(uint p = 0; p < pixels; p++) {
                    const uint8_t* source = png.data + p * png.info.bytesPerPixel;
                    rgba[p * 4 + 0] = source[0];
                    rgba[p * 4 + 1] = source[1];
                    rgba[p * 4 + 2] = source[2];
                    rgba[p * 4 + 3] = png.info.colorType == 6 ? source[3] : 255;
                  }
                  dumper.packImage[index] = rgba;
                  dumper.packImageWidth[index] = png.info.width;
                  dumper.packImageHeight[index] = png.info.height;
                  dumper.packPad[index] = padColumn;
                  dumper.packScale = scaleColumn;
                }
              }
            }
          }
          fclose(fp);
          if(dumper.packCount) {
            char stamp[32] = "run";
            time_t now = ::time(nullptr);
            if(struct tm* local = localtime(&now)) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", local);
            string recPath = {packPath, "/recognized-", stamp, ".csv"};
            dumper.recognizedFile = fopen(recPath, "wb");
            if(dumper.recognizedFile) {
              fputs("frame,id,x,y,width,height\n", dumper.recognizedFile);
              string unrecPath = {packPath, "/unrecognized-", stamp, ".csv"};
              dumper.unrecognizedFile = fopen(unrecPath, "wb");
              if(dumper.unrecognizedFile) fputs("frame,id,x,y,width,height,signature\n", dumper.unrecognizedFile);
              dumper.packEnabled = true;
            }
          }
        }
      }
    }
    if(dumper.packEnabled && dumper.packScale) {
      dumper.hdBuffer = new uint16_t[256 * dumper.packScale * 240 * dumper.packScale]();
      dumper.bgBuffer = new uint16_t[1024 * 256]();  // the sprite-less render writes up to row 246
      dumper.maskPool = new uint8_t[dumper.maskPoolSize]();
      dumper.wonPool = new uint8_t[dumper.maskPoolSize]();
      dumper.packReady = true;
    }
    dumper.enabled = dumper.dumpEnabled || dumper.packEnabled;
    if(dumper.dumpEnabled) {
      //load hashes from earlier runs so they are not dumped again
      string manifestPath = {dumper.directory, "/manifest.csv"};
      if(FILE* fp = fopen(manifestPath, "rb")) {
        char line[512];
        while(fgets(line, sizeof(line), fp)) {
          char* end = nullptr;
          uint64_t hash = strtoull(line, &end, 16);
          if(end == line + 16 && *end == ',') spriteInsert(dumper, hash);
        }
        fclose(fp);
      }
      char stamp[32] = "run";
      time_t now = ::time(nullptr);
      if(struct tm* local = localtime(&now)) strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", local);
      string framesPath = {dumper.directory, "/frames-", stamp, ".csv"};
      dumper.framesFile = fopen(framesPath, "wb");
      if(dumper.framesFile) fputs("frame,oam,hash,x,y,width,height,hflip,vflip,priority,palette\n", dumper.framesFile);
    }
  }
  dumper.frame++;
  if(!dumper.enabled) return;

  static SpriteEntry entries[128];
  uint entryCount = 0;
  uint64_t frameHash = 0xcbf29ce484222325ull;

  for(uint n : range(128)) {
    const auto& object = objects[n];

    static const uint smallWidths [] = { 8,  8,  8, 16, 16, 32, 16, 16};
    static const uint smallHeights[] = { 8,  8,  8, 16, 16, 32, 32, 32};
    static const uint largeWidths [] = {16, 32, 64, 32, 64, 64, 32, 32};
    static const uint largeHeights[] = {16, 32, 64, 32, 64, 64, 64, 32};
    uint width  = !object.size ? smallWidths [io.obj.baseSize] : largeWidths [io.obj.baseSize];
    uint height = !object.size ? smallHeights[io.obj.baseSize] : largeHeights[io.obj.baseSize];

    //skip sprites that are entirely off-screen (games park unused sprites just below the visible area)
    if(object.x > 256 && object.x + width - 1 < 512) continue;
    if(object.y >= vdisp() && object.y + height <= 256) continue;

    uint16 tiledataAddress = io.obj.tiledataAddress;
    if(object.nameselect) tiledataAddress += (1 + io.obj.nameselect) << 12;
    uint characterX = object.character & 15;
    uint characterY = object.character >> 4;

    uint8_t indices[64 * 64];
    for(uint tileY : range(height >> 3)) {
      for(uint tileX : range(width >> 3)) {
        uint tileIndex = ((characterY + tileY & 15) << 4) | (characterX + tileX & 15);
        uint address = (tiledataAddress + (tileIndex << 4)) & 0x7ff0;
        for(uint row : range(8)) {
          uint16 plane01 = vram[address + row + 0];
          uint16 plane23 = vram[address + row + 8];
          for(uint column : range(8)) {
            uint shift = 7 - column;
            uint color = (plane01 >> shift & 1)
                       | (plane01 >> shift + 8 & 1) << 1
                       | (plane23 >> shift & 1) << 2
                       | (plane23 >> shift + 8 & 1) << 3;
            indices[((tileY << 3) + row) * width + (tileX << 3) + column] = color;
          }
        }
      }
    }

    //skip sprites made only of transparent pixels: nothing to dump or replace
    bool opaque = false;
    for(uint i : range(width * height)) if(indices[i]) { opaque = true; break; }
    if(!opaque) continue;

    //FNV-1a 64-bit over dimensions + color indices
    uint64_t hash = 0xcbf29ce484222325ull;
    auto mix = [&](uint8_t byte) { hash ^= byte; hash *= 0x100000001b3ull; };
    mix(width); mix(height);
    for(uint i : range(width * height)) mix(indices[i]);

    {
      SpriteEntry& entry = entries[entryCount++];
      entry.oam = n;
      entry.hash = hash;
      entry.x = object.x >= 256 ? (int)object.x - 512 : (int)object.x;
      entry.y = object.y + height > 256 ? (int)object.y - 256 : (int)object.y;  //negative when wrapping in from the top
      entry.width = width;
      entry.height = height;
      entry.hflip = object.hflip;
      entry.vflip = object.vflip;
      entry.priority = object.priority;
      entry.palette = object.palette;
      for(uint y : range(height)) {
        uint64_t row = 0;
        for(uint x : range(width)) if(indices[y * width + x]) row |= 1ull << x;
        entry.rows[y] = row;
      }
      uint64_t fields[] = {hash, (uint64_t)(entry.x + 512), (uint64_t)(entry.y + 256),
        (uint64_t)entry.hflip, (uint64_t)entry.vflip, (uint64_t)entry.priority, (uint64_t)entry.palette};
      for(uint64_t field : fields) { frameHash ^= field; frameHash *= 0x100000001b3ull; }
    }

    if(!dumper.dumpEnabled) continue;
    if(!spriteInsert(dumper, hash)) continue;

    uint8_t palette[16][3];
    for(uint i : range(16)) {
      uint16 color = cgram[128 + (object.palette << 4) + i];
      uint r = color & 31, g = color >> 5 & 31, b = color >> 10 & 31;
      palette[i][0] = r << 3 | r >> 2;
      palette[i][1] = g << 3 | g >> 2;
      palette[i][2] = b << 3 | b >> 2;
    }

    string name = hex(hash, 16L);
    string file = {dumper.directory, "/", name, ".png"};
    if(!spriteWritePNG(file, width, height, indices, palette)) continue;

    string manifestPath = {dumper.directory, "/manifest.csv"};
    if(FILE* fp = fopen(manifestPath, "ab")) {
      fseek(fp, 0, SEEK_END);
      if(ftell(fp) == 0) fputs("hash,width,height,character,nameselect,palette,hflip,vflip,priority,x,y,frame,oam\n", fp);
      fprintf(fp, "%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", (const char*)name, width, height,
        (uint)object.character, (uint)object.nameselect, (uint)object.palette,
        (uint)object.hflip, (uint)object.vflip, (uint)object.priority,
        (uint)object.x, (uint)object.y, dumper.frame, n);
      fclose(fp);
    }
  }

  if(dumper.packEnabled) recognizeGroups(dumper, entries, entryCount, dumper.frame);

  //frame record: written only when the set of on-screen sprites (or any of their attributes) changed
  if(dumper.framesFile && entryCount && frameHash != dumper.lastFrameHash) {
    dumper.lastFrameHash = frameHash;
    for(uint i : range(entryCount)) {
      const SpriteEntry& entry = entries[i];
      fprintf(dumper.framesFile, "%u,%u,%s,%d,%d,%u,%u,%u,%u,%u,%u\n", dumper.frame, (uint)entry.oam,
        (const char*)hex(entry.hash, 16L), entry.x, entry.y, entry.width, entry.height,
        entry.hflip, entry.vflip, entry.priority, entry.palette);
    }
    fflush(dumper.framesFile);
  }
}

//returns true when the frame was replaced by an enlarged one that contains the replacement images
auto PPU::composePack(const uint16*& data, uint& pitch, uint& width, uint& height, uint& scale) -> bool {
  auto& dumper = spriteDumper;
  bool fresh = dumper.recognizedFresh;
  dumper.recognizedFresh = false;
  if(!dumper.packReady || !fresh) return false;
  if(hd() || hires() || interlace()) return false;

  const uint S = dumper.packScale;
  const uint16* native = output;  //256x240 pixels, 1024 pixels per row
  uint16_t* big = dumper.hdBuffer;
  const uint bigPitch = 256 * S;
  const int rowBase = latch.overscan ? 0 : 7;  //first scanline is drawn on row 7 when overscan is off

  //native frame enlarged with nearest neighbour
  for(uint y = 0; y < 240; y++) {
    uint16_t* row = big + y * S * bigPitch;
    for(uint x = 0; x < 256; x++) {
      uint16_t color = native[y * 1024 + x];
      for(uint k = 0; k < S; k++) row[x * S + k] = color;
    }
    for(uint k = 1; k < S; k++) memcpy(row + k * bigPitch, row, bigPitch * sizeof(uint16_t));
  }

  static uint8_t erased[256 * 240];
  memset(erased, 0, sizeof(erased));
  if(dumper.recognizedCount) {
    //1) which pixels of each group did a sprite win?  read from the layer buffers of the normal render, before they are overwritten
    uint8_t* won[128] = {};
    uint8_t objSource[128] = {}, objPriority[128] = {};
    uint wonUsed = 0;
    for(uint g = 0; g < dumper.recognizedCount; g++) {
      const auto& group = dumper.recognized[g];
      uint area = (uint)group.width * group.height;
      if(!group.opaque || !dumper.wonPool || wonUsed + area > dumper.maskPoolSize) continue;
      won[g] = dumper.wonPool + wonUsed;
      wonUsed += area;
      memset(won[g], 0, area);
      objSource[g] = Source::OBJ1;
      bool first = true;
      for(int ry = 0; ry < group.height; ry++) {
        int py = group.y + rowBase + ry, ly = py - rowBase;
        if(ly < 1 || ly > 239 || py < 0 || py >= 240) continue;
        for(int rx = 0; rx < group.width; rx++) {
          int px = group.x + rx;
          if(px < 0 || px >= 256) continue;
          const Pixel& pixel = lines[ly].above[px];
          if(pixel.source != Source::OBJ1 && pixel.source != Source::OBJ2) continue;
          won[g][ry * group.width + rx] = 1;
          if(first) { objSource[g] = pixel.source; objPriority[g] = pixel.priority; first = false; }
        }
      }
    }

    //2) same frame without the sprite layer: what is behind the sprites
    uint16_t* background = dumper.bgBuffer;
    renderRedirect = background;
    skipObjects = true;
    for(uint y = 1; y <= 239; y++) lines[y].render(field());
    skipObjects = false;
    renderRedirect = nullptr;

    //leftovers: pixels in a thin ring around a replaced group that differ from the sprite-less frame (the sprite or its edge sticks out of the
    //group's box by a pixel or two) are cleaned as well, unless they lie inside the box of another recognized group
    for(uint g = 0; g < dumper.recognizedCount; g++) {
      const auto& group = dumper.recognized[g];
      if(!dumper.packImage[group.pack] || !won[g]) continue;
      for(int py = group.y + rowBase - 3; py < group.y + rowBase + group.height + 3; py++) {
        if(py < 0 || py >= 240) continue;
        for(int px = group.x - 3; px < group.x + group.width + 3; px++) {
          if(px < 0 || px >= 256) continue;
          bool inside = false;
          for(uint k = 0; k < dumper.recognizedCount && !inside; k++) {
            const auto& other = dumper.recognized[k];
            inside = px >= other.x && px < other.x + other.width && py >= other.y + rowBase && py < other.y + rowBase + other.height;
          }
          if(inside || native[py * 1024 + px] == background[py * 1024 + px]) continue;
          uint16_t color = background[py * 1024 + px];
          for(uint sy = 0; sy < S; sy++) for(uint sx = 0; sx < S; sx++) big[(py * S + sy) * bigPitch + px * S + sx] = color;
          erased[py * 256 + px] = 1;
        }
      }
    }

    static uint8_t visible[(320 + 2) * (320 + 2)];
    static uint8_t margin [(320 + 2) * (320 + 2)];
    static uint8_t forbidden[(320 + 2) * (320 + 2)];

    for(uint g = 0; g < dumper.recognizedCount; g++) {
      const auto& group = dumper.recognized[g];
      const uint8_t* image = dumper.packImage[group.pack];
      if(!image || !group.opaque || !won[g]) continue;
      int w = group.width, h = group.height;
      if(w + 2 > 322 || h + 2 > 322) continue;
      uint pad = dumper.packPad[group.pack];
      if((w + 2 * pad) * S != dumper.packImageWidth[group.pack] || (h + 2 * pad) * S != dumper.packImageHeight[group.pack]) continue;
      int stride = w + 2;
      int top = group.y + rowBase;

      //visible: a sprite was the winning layer there
      memset(visible, 0, stride * (h + 2));
      for(int ry = 0; ry < h; ry++) for(int rx = 0; rx < w; rx++) visible[(ry + 1) * stride + rx + 1] = won[g][ry * w + rx];
      //erase the original sprite from the enlarged frame
      for(int ry = 0; ry < h; ry++) for(int rx = 0; rx < w; rx++) {
        if(!visible[(ry + 1) * stride + rx + 1]) continue;
        int px = group.x + rx, py = top + ry;
        uint16_t color = background[py * 1024 + px];
        for(uint sy = 0; sy < S; sy++) for(uint sx = 0; sx < S; sx++) big[(py * S + sy) * bigPitch + px * S + sx] = color;
        erased[py * 256 + px] = 1;
      }
      //hidden: pixels the original sprite had but that another layer covered
      memset(margin, 0, stride * (h + 2));
      for(int ry = 0; ry < h; ry++) for(int rx = 0; rx < w; rx++) {
        int px = group.x + rx, py = top + ry;
        if(px < 0 || px >= 256 || py < 0 || py >= 240) continue;
        if(group.opaque[ry * w + rx] && !visible[(ry + 1) * stride + rx + 1]) margin[(ry + 1) * stride + rx + 1] = 1;
      }

      //debugging aid: frames where a large share of the group counts as hidden are saved to debug-<n>.bin in the pack directory
      //(header: x, y, w, h, rowBase, hiddenPixels, opaquePixels, then 256x240 native frame, 256x240 sprite-less frame, w*h opaque bytes, w*h won bytes)
      if(dumper.debugCaptured < 6) {
        uint hidden = 0, opaquePixels = 0;
        for(int i = 0; i < w * h; i++) opaquePixels += group.opaque[i];
        for(int ry = 0; ry < h; ry++) for(int rx = 0; rx < w; rx++) hidden += margin[(ry + 1) * stride + rx + 1];
        if(hidden * 8 > opaquePixels) {
          string debugPath = {dumper.packDirectory, "/debug-", dumper.debugCaptured++, ".bin"};
          if(FILE* fp = fopen(debugPath, "wb")) {
            int32_t header[7] = {group.x, group.y, w, h, rowBase, (int32_t)hidden, (int32_t)opaquePixels};
            fwrite(header, 4, 7, fp);
            for(uint y = 0; y < 240; y++) fwrite(native + y * 1024, 2, 256, fp);
            for(uint y = 0; y < 240; y++) fwrite(background + y * 1024, 2, 256, fp);
            fwrite(group.opaque, 1, w * h, fp);
            fwrite(won[g], 1, w * h, fp);
            fclose(fp);
          }
        }
      }

      //hidden pixels that form only thin strips (the outline of the original sprite is often not drawn, shadows, etc.) are not an
      //occlusion. Only blocks of hidden pixels that are solid over a 5x5 area count as covered by another layer; the replacement
      //is not drawn on them nor within 2 pixels around them
      static uint8_t covered[(320 + 4) * (320 + 4)];
      const int cs = w + 4;
      memset(covered, 0, (size_t)cs * (h + 4));
      for(int ry = 0; ry < h; ry++) for(int rx = 0; rx < w; rx++) {
        bool solid = true;
        for(int dy = -2; dy <= 2 && solid; dy++) for(int dx = -2; dx <= 2; dx++) {
          int yy = ry + dy, xx = rx + dx;
          if(yy < 0 || yy >= h || xx < 0 || xx >= w || !margin[(yy + 1) * stride + xx + 1]) { solid = false; break; }
        }
        covered[(ry + 2) * cs + rx + 2] = solid;
      }
      memset(forbidden, 0, stride * (h + 2));
      for(int ry = 0; ry < h; ry++) for(int rx = 0; rx < w; rx++) {
        uint8_t any = 0;
        for(int dy = 0; dy <= 4 && !any; dy++) for(int dx = 0; dx <= 4; dx++) any |= covered[(ry + dy) * cs + rx + dx];
        forbidden[(ry + 1) * stride + rx + 1] = any;
      }

      //draw the replacement; its color goes through the same window / color math / brightness path as the original sprite
      uint imageWidth = (w + 2 * pad) * S, imageHeight = (h + 2 * pad) * S;
      for(uint iy = 0; iy < imageHeight; iy++) {
        int hy = (top - (int)pad) * (int)S + (int)iy;
        if(hy < 0 || hy >= (int)(240 * S)) continue;
        int ny = hy / (int)S, ly = ny - rowBase;
        if(ly < 1 || ly > 239) continue;
        auto& line = lines[ly];
        auto luma = lightTable[line.io.displayBrightness];
        for(uint ix = 0; ix < imageWidth; ix++) {
          const uint8_t* pixel = image + (iy * imageWidth + ix) * 4;
          uint alpha = pixel[3];
          if(!alpha) continue;
          int bx = (int)(ix / S) - (int)pad, by = (int)(iy / S) - (int)pad;  //native pixel relative to the group's box
          if(bx >= -1 && bx <= w && by >= -1 && by <= h && forbidden[(by + 1) * stride + bx + 1]) continue;
          int hx = (group.x - (int)pad) * (int)S + (int)ix;
          if(hx < 0 || hx >= (int)bigPitch) continue;
          int nx = hx / (int)S;
          Pixel sprite;
          sprite.source = objSource[g];
          sprite.priority = objPriority[g];
          sprite.color = (pixel[2] >> 3) << 10 | (pixel[1] >> 3) << 5 | (pixel[0] >> 3);
          uint16_t source = luma[line.pixel(nx, sprite, line.below[nx])];
          uint16_t& target = big[hy * bigPitch + hx];
          if(alpha < 255) {
            uint r = ((source      & 31) * alpha + (target      & 31) * (255 - alpha)) / 255;
            uint gr= ((source >> 5 & 31) * alpha + (target >> 5 & 31) * (255 - alpha)) / 255;
            uint b = ((source >> 10& 31) * alpha + (target >> 10& 31) * (255 - alpha)) / 255;
            source = b << 10 | gr << 5 | r;
          }
          target = source;
        }
      }
    }
  }

  //debugging aid: pixels around a replaced group that differ from the sprite-less frame but were not erased (leftovers of the original);
  //the first frames with such pixels are saved to leak-<n>.bin (header: x, y, w, h, rowBase, leakPixels, 0; native frame, sprite-less frame, erased mask; all 256x240)
  if(dumper.recognizedCount && dumper.leakCaptured < 6) {
    for(uint g = 0; g < dumper.recognizedCount && dumper.leakCaptured < 6; g++) {
      const auto& group = dumper.recognized[g];
      if(!dumper.packImage[group.pack]) continue;
      uint leak = 0;
      for(int py = group.y + rowBase - 6; py < group.y + rowBase + group.height + 6; py++) {
        if(py < 0 || py >= 240) continue;
        for(int px = group.x - 6; px < group.x + group.width + 6; px++) {
          if(px < 0 || px >= 256) continue;
          if(!erased[py * 256 + px] && native[py * 1024 + px] != dumper.bgBuffer[py * 1024 + px]) leak++;
        }
      }
      if(leak >= 3) {
        string leakPath = {dumper.packDirectory, "/leak-", dumper.leakCaptured++, ".bin"};
        if(FILE* fp = fopen(leakPath, "wb")) {
          int32_t header[7] = {group.x, group.y, group.width, group.height, rowBase, (int32_t)leak, 0};
          fwrite(header, 4, 7, fp);
          for(uint y = 0; y < 240; y++) fwrite(native + y * 1024, 2, 256, fp);
          for(uint y = 0; y < 240; y++) fwrite(dumper.bgBuffer + y * 1024, 2, 256, fp);
          fwrite(erased, 1, 256 * 240, fp);
          fclose(fp);
        }
      }
    }
  }

  data = big;
  pitch = bigPitch;
  width = 256 * S;
  height = 240 * S;
  scale = S;
  return true;
}
