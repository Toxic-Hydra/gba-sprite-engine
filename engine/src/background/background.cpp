//
// Created by Wouter Groeneveld on 28/07/18.
//

#include <libgba-sprite-engine/gba/tonc_memmap.h>
#include <libgba-sprite-engine/gba/tonc_core.h>
#include <stdexcept>
#include <libgba-sprite-engine/allocator.h>
#ifdef CODE_COMPILED_AS_PART_OF_TEST
#include <libgba-sprite-engine/gba/tonc_core_stub.h>
#else
#include <libgba-sprite-engine/gba/tonc_core.h>
#endif
#include <libgba-sprite-engine/background/background.h>
#include <libgba-sprite-engine/background/text_stream.h>

#define TRANSPARENT_TILE_NUMBER 192     // as shown in mGBA, next "free" tile after text. Hardcoded indeed.

// WHY using this instead of Allocation?
// Because each char block seems to be 16K and there are 4 - there are also 4 backgrounds.
// Use the bgIndex as a hardcoded char block and let the background decide on the map screen block.
void* screen_block(unsigned long block) {
    return (void*) (0x6000000 + (block * 0x800));
}

void* char_block(unsigned long block) {
    return (void*) (0x6000000 + (block * 0x4000));
}

void Background::updateMap(const void *map) {
    this->map = map;
    dma3_cpy(screen_block(screenBlockIndex), this->map, this->mapSize);
}

void Background::persist() {
    dma3_cpy(char_block(charBlockIndex), this->data, this->size);

    if(this->map) {
        dma3_cpy(screen_block(screenBlockIndex), this->map, this->mapSize);
    }

    buildRegister();
}

void Background::clearData() {
    this->clearMap();
    int empty[this->size];
    dma3_cpy(char_block(charBlockIndex), empty, this->size);
}

void Background::clearMap() {
    volatile auto ptr = &se_mem[screenBlockIndex][0];
    for (int i = 0; i < this->mapSize; i++) {
        ptr[i] = TRANSPARENT_TILE_NUMBER;
    }
}

u32 Background::getBgControlRegisterIndex() {
    switch(bgIndex) {
        case 0: return 0x0008;
        case 1: return 0x000A;
        case 2: return 0x000C;
        case 3: return 0x000E;
    }
    throw std::runtime_error("unknown bg index");
}

void Background::buildRegister() {
    *(vu16*)(REG_BASE+getBgControlRegisterIndex()) =
                 bgIndex |        /* priority, 0 is highest, 3 is lowest */
                 (charBlockIndex << 2) |    /* the char block the image data is stored in */
                 (0 << 6)  |       /* the mosaic flag */
                 (1 << 7)  |       /* color mode, 0 is 16 colors, 1 is 256 colors */
                 (screenBlockIndex << 8) |       /* the screen block the tile data is stored in */
                 (1 << 13) |       /* wrapping flag */
                 (mapLayout << 14);
}

void Background::scroll(int x, int y) {
    REG_BG_OFS[bgIndex].x = x;
    REG_BG_OFS[bgIndex].y = y;
}

void Background::scrollSpeed(int dx, int dy) {
    REG_BG_OFS[bgIndex].x += dx;
    REG_BG_OFS[bgIndex].y += dy;
}

// TileMap collisions pretty much entirely from: https://wiki.nycresistor.com/wiki/GB101:Collision_Detection
int Background::se_index(int x, int y) { //It seems to me that it doesn't accurately grab the index.
    //Adjust for map layout
    //Keep in mind this only works on 64 x 64 maps.
    //We likely need to adjust base for a smaller map but i can not figure out
    //why a base of 256 is used in the first place.

    int base = 0;
    if(x >= MAP_WIDTH/2) {
        x -= MAP_WIDTH/2;
        base = 32*32;
    }

    return base + (y>>3<<5) + (x>>3);
    /*int n = x + y*32;
    if (x >= 32)
        n += 0x03E0;
    if ( y >= 32 && mapLayout == MAPLAYOUT_64X64)
        n += 0x0400;

    return n;*/

}

int Background::point_collision(int x, int y) {
    int i = se_index(x,y);
    int tid = se_mem[screenBlockIndex][i]; //TEST USING SCREENBLOCK INDEX
    tid = tid & 0xFF;

    return (    //Collidable tile id's go here.
        tid >= 1
    );
}


// Boxes have two points, we're essentially passing in the bounding box into this test
// x1 y1 top left. bX bY Bounding box bottom right. xofs yofs are current dx dy
int Background::collision_test(int x1, int y1, int bX, int bY, int xofs, int yofs) {
    int result = 0;

    if(xofs > 0 && !(bX+xofs & 7)) {
        if( point_collision(bX + xofs, y1) || point_collision(bX = xofs, bY)) {
            result = COLLISION_X;
        }
    }
    else if( xofs < 0 && !(bX - xofs & 7)) {
        if(point_collision(x1 + xofs, y1) || point_collision(x1 + xofs, bY)) {
            result = COLLISION_X;
        }
    }

    //now check Y
    if(yofs > 0 && !(bY + yofs & 7)) {
        if(point_collision(x1, bY + yofs) || point_collision(bX, bY + yofs)) {
            result = result | COLLISION_Y;
        }
    }
    else if(yofs < 0 && !(bY - yofs & 7)) {
        if(point_collision(x1, y1 + yofs) || point_collision(bX, y1 + yofs)) {
            result = result | COLLISION_Y;
        }
    }

    return result;
}