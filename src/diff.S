#include <xtensa/config/core-isa.h>
#include <xtensa/config/core-matmap.h>

.text
.align  4
.global epd_interlace_4bpp_line_VE
.type   epd_interlace_4bpp_line_VE,@function

//        // CRASH AND BURN for debugging
//        EE.MOVI.32.A q3, a2, 0
//        EE.MOVI.32.A q3, a3, 1
//        EE.MOVI.32.A q3, a4, 2
//        EE.MOVI.32.A q3, a5, 3
//        l8ui a10, a10, 0

// bool interlace_line(
//     const uint8_t *to,
//     const uint8_t *from,
//     uint8_t *interlaced,
//     int* min_dirty_x,
//     int* max_dirty_x,
//     int fb_width
// )
epd_interlace_4bpp_line_VE: 
// to   - a2
// from   - a3
// interlaced   - a4
// min_x      - a5
// max_x      - a6
// fb_width      - a7

    entry	a1, 32

    // divide by 32 for loop count
    srli a11, a7, 5

    movi.n a10, 0xF0F0F0F0;
    EE.MOVI.32.Q q6,a10,0
    EE.MOVI.32.Q q6,a10,1
    EE.MOVI.32.Q q6,a10,2
    EE.MOVI.32.Q q6,a10,3

    movi.n a10, 0x0F0F0F0F
    EE.MOVI.32.Q q7,a10,0
    EE.MOVI.32.Q q7,a10,1
    EE.MOVI.32.Q q7,a10,2
    EE.MOVI.32.Q q7,a10,3

    // put 4 into shift amount
    movi.n a10, 4
    WSR.SAR a10

    // Instructions sometimes are in an unexpected order
    // for best pipeline utilization
    loopnez a11, .loop_end_difference

        EE.VLD.128.IP q0, a2, 16
        EE.VLD.128.IP q1, a3, 16

        // mask out every second value
        EE.ANDQ q2, q0, q7
        EE.ANDQ q3, q0, q6
        EE.ANDQ q4, q1, q7
        EE.ANDQ q5, q1, q6

        // shift vectors to align
        EE.VSL.32 q2, q2
        EE.VSR.32 q5, q5

        // the right shift sign-extends, 
        // so we make sure the resulting shift is logical by masking again
        EE.ANDQ q5, q5, q7

        // Combine "from" and "to" nibble
        EE.ORQ q2, q2, q4
        EE.ORQ q3, q3, q5

        // Zip masked out values together
        EE.VZIP.8 q2, q3
        
        // store interlaced buffer data
        EE.VST.128.IP q2, a4, 16
        EE.VST.128.IP q3, a4, 16

.loop_end_difference:

    movi.n	a2, 1 // return "true"
    retw.n
