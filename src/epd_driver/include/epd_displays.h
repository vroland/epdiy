#pragma once
#include "epd_driver.h"

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Default EPD displays struct(s)
 * Important: Definition should match table in epdiy Readme
 * 
 *            Not sure if all Waveforms correspond all this needs defaults to be double-checked
 */
const EpdDisplay ED047TC1 = {
  .name = "Lilygo EPD47",
  .waveform = &epdiy_ED047TC1,
  .width = 960,
  .height = 540
};

const EpdDisplay ED060SC4 = {
    .name = "ED060SC4",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED097OC4 = {
    .name = "ED097OC4",
    .waveform = &epdiy_ED097OC4,
    .width = 1200,
    .height = 825
};

const EpdDisplay ED097TC2 = {
    .name = "ED097TC2",
    .waveform = &epdiy_ED097TC2,
    .width = 1200,
    .height = 825
};

const EpdDisplay ED097OC1 = {
    .name = "ED097TC1",
    .waveform = &epdiy_ED097OC4,
    .width = 1200,
    .height = 825
};

const EpdDisplay ED052TC2 = {
    .name = "ED052TC2 5.2\"",
    .waveform = &epdiy_ED047TC1,
    .width = 960,
    .height = 540
};

const EpdDisplay ED050SC5 = {
    .name = "ED050SC5 5\"",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED050SC3 = {
    .name = "ED050SC3",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED133UT2 = {
    .name = "ED133UT2",
    .waveform = &epdiy_ED133UT2,
    .width = 1600,
    .height = 1200
};
// Here not sure if it's worth to add 6 clons of EPDs that all have same settings (for now)
const EpdDisplay ED060XC3 = {
    .name = "ED060XC3",
    .waveform = &epdiy_ED060XC3,
    .width = 1024,
    .height = 758
};

const EpdDisplay ED060XD4 = {
    .name = "ED060XD4",
    .waveform = &epdiy_ED060XC3,
    .width = 1024,
    .height = 758
};

const EpdDisplay ED060XC5 = {
    .name = "ED060XC5",
    .waveform = &epdiy_ED060XC3,
    .width = 1024,
    .height = 758
};

const EpdDisplay ED060XD6 = {
    .name = "ED060XD6",
    .waveform = &epdiy_ED060XC3,
    .width = 1024,
    .height = 758
};

const EpdDisplay ED060XH2 = {
    .name = "ED060XH2",
    .waveform = &epdiy_ED060XC3,
    .width = 1024,
    .height = 758
};

const EpdDisplay ED060XC9 = {
    .name = "ED060XC9",
    .waveform = &epdiy_ED060XC3,
    .width = 1024,
    .height = 758
};

const EpdDisplay ED060KD1 = {
    .name = "ED060KD1",
    .waveform = &epdiy_ED060XC3,
    .width = 1072,
    .height = 1448
};

const EpdDisplay ED060KC1 = {
    .name = "ED060KC1",
    .waveform = &epdiy_ED060XC3,
    .width = 1072,
    .height = 1448
};

const EpdDisplay ED060SCF = {
    .name = "ED060SCF",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED060SCN = {
    .name = "ED060SCN",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED060SCP = {
    .name = "ED060SCP",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED060SC7 = {
    .name = "ED060SC7",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED060SCG = {
    .name = "ED060SCG",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED060SCE = {
    .name = "ED060SCE",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED060SCM = {
    .name = "ED060SCM",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};

const EpdDisplay ED060SCT = {
    .name = "ED060SCT",
    .waveform = &epdiy_ED060SC4,
    .width = 800,
    .height = 600
};
#ifdef __cplusplus
}
#endif
