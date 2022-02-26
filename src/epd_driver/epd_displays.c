#include "epd_displays.h"

void epdiy_display_default_configs() {
   ED047TC1.name = "Lilygo EPD47";
   ED047TC1.waveform = epdiy_ED047TC1;
   ED047TC1.width = 960;
   ED047TC1.height = 540;

   ED060SC4.name = "ED060SC4";
   ED060SC4.waveform = epdiy_ED060SC4;
   ED060SC4.width = 800;
   ED060SC4.height = 600;

   ED097OC4.name = "ED097OC4";
   ED097OC4.waveform = epdiy_ED097OC4;
   ED097OC4.width = 1200;
   ED097OC4.height = 825;

   ED097TC2.name = "ED097TC2";
   ED097TC2.waveform = epdiy_ED097TC2;
   ED097TC2.width = 1200;
   ED097TC2.height = 825;

   ED097OC1.name = "ED097TC1";
   ED097OC1.waveform = epdiy_ED097OC4;
   ED097OC1.width = 1200;
   ED097OC1.height = 825;
}