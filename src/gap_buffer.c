#include "gap_buffer.h"
#include "editor.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GapBuffer *gb_create(size_t GAP_SIZE) {
    GapBuffer *gb = (GapBuffer *)malloc(sizeof(GapBuffer));
    if(!gb) return NULL;

    gb->point = NULL;
    gb->buffer = NULL;
    gb->bufend = NULL;
    gb->gapstart = NULL;
    gb->gapend = NULL;
    gb->GAP_SIZE = GAP_SIZE;

    gb_init_buffer(gb, GAP_SIZE);

    return gb; 
}

int gb_init_buffer(GapBuffer *gb,size_t size) {
    if(gb->buffer) {
        free(gb->buffer);
    }

    gb->buffer = malloc(size);
    if(!gb->buffer) {
        return 0;
    }
    gb->point = gb->buffer;
    gb->gapstart = gb->buffer; // initial gapsize = full buffer
    gb->gapend = gb->buffer + size;
    gb->bufend = gb->gapend;

    return 1;
} 

int gb_copy_bytes(GapBuffer *gb,char *dest,char *src,size_t len) {
    if((dest == src) || len == 0) {
        return 1;
    }
    if((src + len) > gb->bufend) return 0;
    memmove(dest,src,len);
    return 1;
}

void gb_move_gap_to_point(GapBuffer *gb) {
    if(gb->point == gb->gapstart) {
        return;
    }
    if(gb->point == gb->gapend) {
        gb->point = gb->gapstart;
        return;
    }
    if(gb->point < gb->gapstart) {
        //move gap towards left 
        //move the point over by gapsize

        size_t n = gb->gapstart - gb->point; 

        gb_copy_bytes(gb,gb->gapend - n,gb->point,n);
        gb->gapstart -= n;
        gb->gapend   -= n;
    }
    else {
        //Since the point will be after the gap ,
        //find distance gapend and the point
        //and thats how much we move from gapend to gapstart
        //we are moving the bytes after the gapend till the point to the start aste

        size_t n = gb->point - gb->gapend;

        gb_copy_bytes(gb,gb->gapstart,gb->gapend,n);
        gb->gapstart += n;
        gb->gapend   += n;
    }

    gb->point = gb->gapstart;

}

void gb_expand_buffer(GapBuffer *gb,size_t size) {
    char *origBuffer = gb->buffer;

    size_t newBufferSize =
        (gb->bufend - gb->buffer) + size + gb->GAP_SIZE;

    gb->buffer = (char *)realloc(gb->buffer,newBufferSize);
    if(!gb->buffer) return;

    ptrdiff_t diff = gb->buffer - origBuffer;

    gb->point    += diff;
    gb->bufend   += diff;
    gb->gapstart += diff;
    gb->gapend   += diff;
}

void gb_expand_gap(GapBuffer *gb,size_t size) {
    if(size > gb_size_of_gap(gb)) {
        size+=gb->GAP_SIZE;
        gb_expand_buffer(gb,size);
        //move the text after the gap to the right
        gb_copy_bytes(
            gb,
            gb->gapend+size,
            gb->gapend,
            gb->bufend-gb->gapend
        );

        gb->gapend+=size;
        gb->bufend+=size;
    }
}

size_t gb_size_of_gap(GapBuffer *gb){
    return (size_t)(gb->gapend - gb->gapstart);
}

size_t gb_buffer_size(GapBuffer *gb) {
    return (size_t)((gb->bufend - gb->buffer) - (gb->gapend - gb->gapstart));
}

size_t gb_point_offset(GapBuffer *gb) { //where is the cursor from the start
    if(gb->point >= gb->gapend)
        return (size_t)((gb->point - gb->buffer) - (gb->gapend - gb->gapstart));
    else 
        return (size_t)(gb->point - gb->buffer);
}

void gb_set_point(GapBuffer *gb,size_t offset){
    gb->point = gb->buffer + offset;

    //if the position lands on a gap,we should skip it (add the size)
    if(gb->point >= gb->gapstart) { 
        gb->point += (gb->gapend - gb->gapstart);
    }
}

void gb_free(GapBuffer *gb){
    if(!gb) return;
    free(gb->buffer);
    free(gb);
}

char gb_prev_char(GapBuffer *gb) {
    if(gb->point == gb->gapend) {
        gb->point = gb->gapstart;
    }
    return *(--gb->point);
}

char gb_next_char(GapBuffer *gb) {
    if(gb->point == gb->gapstart) {
        gb->point = gb->gapend;
    }
    gb->point++;
    //if we land on the gapstart again
    if(gb->point == gb->gapstart) {
        gb->point = gb->gapend;
    }
    return *gb->point;
}

void gb_replace_char(GapBuffer *gb, char ch) {
    if(gb->point == gb->gapstart) {
        gb->point = gb->gapend;
    }

    //if there is no charecter under the cursor it should do nothing
    if(gb->point == gb->bufend) {
        return;
    }
    *gb->point = ch;
}

void gb_put_char(GapBuffer *gb, char ch) {
    //this is the func that we are going to pass in the UI part not the insert function
    //it inserts and moves the cursor forward
    //insert_char function is for datastructure responsibility 
    //put_char is for editor behavior

    //make sure the gap is at the cursor
    if(gb->point!=gb->gapstart) {
        gb_move_gap_to_point(gb);
    }

    gb_insert_char(gb,ch);

    gb->point = gb->gapstart; 
}

char gb_get_char(GapBuffer *gb) {
    //if the point is at gapstart,jump to first real charecter
    if(gb->point == gb->gapstart) {
        gb->point = gb->gapend;
    }

    return *gb->point;
}

void gb_insert_char(GapBuffer *gb,char ch) {
    if(gb->point != gb->gapstart) {
        gb_move_gap_to_point(gb);
    }
    if(gb->gapstart == gb->gapend) {
        gb_expand_gap(gb,1);
    }
    *(gb->gapstart++) = ch; 
}

void gb_insert_string(GapBuffer *gb, const char *str, size_t len) {
    if(gb->point != gb->gapstart) {
        gb_move_gap_to_point(gb);
    }
    //making sure it has enough gap
    if((size_t)(gb->gapend - gb->gapstart) < len) {
        gb_expand_gap(gb,len);
    }

    for(size_t i=0;i<len;i++) {
        gb->gapstart[i] = str[i];
    }
    gb->gapstart+=len;
    gb->point = gb->gapstart;
}

void gb_delete_chars(GapBuffer *gb, size_t count) {
    //deleting just means to increase the gapsize
    if(gb->point!=gb->gapstart) {
        gb_move_gap_to_point(gb);
    }

    size_t available = gb->gapstart - gb->buffer;
    if(count > available) {
        count = available; //limiting the 
    }
    //extend the gap to cover the deleted charecters
    gb->gapstart-=count;
    gb->point = gb->gapstart;
}

int gb_load_file(GapBuffer *gb,FILE *file) {
    if(!gb || !file) {
        return 0;
    }

    //clear the existing buffer
    gb_init_buffer(gb,gb->GAP_SIZE);

    //make the cursor move to the end 
    //so that we can find the file size
    fseek(file,0,SEEK_END);
    size_t size = ftell(file); //no of bytes form the start since the cursor is at end it is file size
    rewind(file); //make the cursor point to start again

    gb_expand_gap(gb,size); //make enough space

    size_t read = fread(gb->gapstart,1,size,file); //reading byte by byte
    gb->gapstart+=read;
    cursor = 0;
    gb_set_point(gb,0);

    return 1;

}

int  gb_save_to_file(GapBuffer *gb, FILE *file) {
    if(!gb || !file) {
        return 0;
    }

    //saving means skipping the gap entirely
    //the gap should never part of the text
    
    //write text before the gap
    size_t left = gb->gapstart - gb->buffer;
    fwrite(gb->buffer,1,left,file);

    //write after the gap
    size_t right = gb->bufend - gb->gapend;
    fwrite(gb->gapend,1,right,file);

    return 1;

}

