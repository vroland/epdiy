#include <xtensa/config/core-isa.h>
#include <xtensa/config/core-matmap.h>

.text
.align  4
.global calc_epd_input_1ppB_64k_ve
.type   calc_epd_input_1ppB_64k_ve,@function

//        // CRASH AND BURN for debugging
//        EE.MOVI.32.A q3, a2, 0
//        EE.MOVI.32.A q3, a3, 1
//        EE.MOVI.32.A q3, a4, 2
//        EE.MOVI.32.A q3, a5, 3
//        l8ui a10, a10, 0

// void calc_epd_input_1ppB_64k(
//    const uint32_t *ld,
//    uint8_t *epd_input,
//    const uint8_t *conversion_lut,
//    uint32_t epd_width
//);
calc_epd_input_1ppB_64k_ve: 
// input   - a2
// output   - a3
// lut   - a4
// len      - a5

    entry	a1, 32
    srli a5, a5, 4


    // bitmasks for bit shift by multiplication
    movi a10, 0x40001000
    EE.MOVI.32.Q q4,a10,0
    movi a10, 0x04000100
    EE.MOVI.32.Q q4,a10,1
    movi a10, 0x00400010
    EE.MOVI.32.Q q4,a10,2
    movi a10, 0x00040001
    EE.MOVI.32.Q q4,a10,3

    // have zero in a10
    movi a10, 0

    // TODO: can be moved out
    EE.ZERO.Q q0

    EE.VLD.128.IP q1, a2, 16

    loopnez a5, .loop_end_lut_lookup

        // q1, q0 contain the input bytes, zero-extended to 16 bytes
        EE.VZIP.8 q1, q0

        // load 32-bit LUT results into q2, q3
        EE.LDXQ.32 q2, q0, a4, 0, 6
        EE.LDXQ.32 q2, q0, a4, 1, 7
        EE.LDXQ.32 q2, q0, a4, 2, 4
        EE.LDXQ.32 q2, q0, a4, 3, 5
        EE.LDXQ.32 q3, q0, a4, 0, 2
        EE.LDXQ.32 q3, q0, a4, 1, 3
        EE.LDXQ.32 q3, q0, a4, 2, 0
        EE.LDXQ.32 q3, q0, a4, 3, 1

        EE.ZERO.ACCX
        
        // zip to have 16bit LUT results in q2, q3 zeroes
        EE.VUNZIP.16 q2, q3

        EE.VMULAS.U16.ACCX q2,q4

        // load 32-bit LUT results into q3, q0
        // We interleave the data loading with retrieving the result
        // from the accumulator, to have better pipeline utilization
        EE.LDXQ.32 q2, q1, a4, 0, 6
        EE.LDXQ.32 q2, q1, a4, 1, 7
        EE.LDXQ.32 q2, q1, a4, 2, 4
        EE.LDXQ.32 q2, q1, a4, 3, 5
        EE.LDXQ.32 q0, q1, a4, 0, 2
        EE.LDXQ.32 q0, q1, a4, 1, 3
        EE.LDXQ.32 q0, q1, a4, 2, 0
        EE.LDXQ.32 q0, q1, a4, 3, 1

        // shift result by zero and store in a6
        EE.SRS.ACCX a6, a10, 0

        EE.ZERO.ACCX
        
        // zip to have 16bit LUT results in q2, a0 zeroes
        EE.VUNZIP.16 q2, q0

        slli a6, a6, 16

        // FIXME: Loads beyond bounds
        EE.VMULAS.U16.ACCX.LD.IP q1, a2, 16, q2, q4

        // shift result by zero and store in a7
        EE.SRS.ACCX a7, a10, 0

        // combine results
        or a6, a6, a7
        s32i      a6, a3, 0
        addi.n a3, a3, 4


.loop_end_lut_lookup:
    movi.n	a2, 0 // return status ESP_OK
    retw.n

