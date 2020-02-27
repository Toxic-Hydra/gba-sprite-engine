//
// Created by Wouter Groeneveld on 28/07/18.
//

#include <libgba-sprite-engine/gba/tonc_memmap.h>

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
                 (0 << 13) |       /* wrapping flag */
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
int Background::se_index(int x, int y, int backx, int backy) { 
    //THanks to Ian Finlayson for this: http://ianfinlayson.net/class/cpsc305/notes/15-sprites
    //I was having issues accurately grabbing index and was not properly accounting for scroll like he is.
    
    x += backx;
    y += backy;

    //convert screen to tile 
    x >>= 3;
    y >>= 3;

    while (x >= this->MAP_WIDTH ) {
        x -= this->MAP_WIDTH;
    }
    while (y >= this->MAP_HEIGHT ) {
        y -= this->MAP_HEIGHT;
    }
    while (x < 0) {
        x += this->MAP_WIDTH;
    }
    while (y < 0) {
        y += this->MAP_HEIGHT;
    }
    
    int offset = 0;

    if (this->MAP_WIDTH == 64 && x >= 32) {
        x -= 32;
        offset += 0x400;
    }
    if (this->MAP_HEIGHT == 64 && y >= 32) {
        y -=32;

        if (this->MAP_WIDTH == 64) {
            offset += 0x800;
        }
        else {
            offset += 0x400;
        }
    }

    int index = y * 32 + x;

    return index + offset;

}

int Background::point_collision(int x, int y, int backx, int backy) {
    int i = se_index(x,y, backx, backy);
    int tid = se_mem[screenBlockIndex][i]; 
    tid = tid & 0xFF;

    return (    //Collidable tile id's go here.
        tid == 1
    );
}


// Boxes have two points, we're essentially passing in the bounding box into this test
// x1 y1 top left. bX bY Bounding box bottom right. xofs yofs are current dx dy
int Background::collision_test(int x1, int y1, int bX, int bY, int xofs, int yofs, int backx, int backy) {
    int result = 0;

    if(xofs > 0 ) {
        if( point_collision(bX + xofs, y1, backx, backy) || point_collision(bX + xofs, bY, backx, backy)) {
            result = COLLISION_X;
        }
    }
    else if( xofs < 0 ) {
        if(point_collision(x1 + xofs, y1, backx, backy) || point_collision(x1 + xofs, bY, backx, backy)) {
            result = COLLISION_X;
        }
    }

    //now check Y
    if(yofs > 0 ) {
        if(point_collision(x1, bY + yofs, backx, backy) || point_collision(bX, bY + yofs, backx, backy)) {
            result = result | COLLISION_Y;
        }
    }
    else if(yofs < 0 ) {
        if(point_collision(x1, y1 + yofs, backx, backy) || point_collision(bX, y1 + yofs, backx, backy)) {
            result = result | COLLISION_Y;
        }
    }

    return result;
}

/*
void Background::updateCollisions(int x1, int y1, int bX, int bY, int xofs, int yofs)
{
    
}*/