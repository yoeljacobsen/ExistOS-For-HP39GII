#include "FreeRTOS.h"
#include "task.h"

#include "board_up.h"
#include "regspower.h"
#include "regslradc.h"
#include "interrupt_up.h"
#include "hw_irq.h"
#include "regsrtc.h"

#include "keyboard_up.h"
#include "../debug.h"

bool g_chargeEnable = false;

void portChargeEnable(bool enable)
{
    g_chargeEnable = enable;
    if(g_chargeEnable)
    {
        HW_POWER_CHARGE.B.PWD_BATTCHRG = 0;
        HW_POWER_VDDDCTRL.B.DISABLE_FET = 0;
        HW_POWER_5VCTRL.B.ENABLE_DCDC = 1;
    }else{
        HW_POWER_CHARGE.B.PWD_BATTCHRG = 1;
        HW_POWER_VDDDCTRL.B.DISABLE_FET = 1;
        HW_POWER_5VCTRL.B.ENABLE_DCDC = 0;
    }
}

void portPowerIRQ(uint32_t nirq)
{
    switch (nirq)
    {
    case HW_IRQ_VDD5V:
        INFO("USB 5V Connect\\Disconnect\n");
        break;
    case HW_IRQ_VDD18_BRNOUT:
        INFO("VDD 1.8v Brownout\n");
        break;
    case HW_IRQ_VDDD_BRNOUT:
        INFO("VDDD Brownout\n");
        break;
    case HW_IRQ_VDDIO_BRNOUT:
        INFO("VDDIO Brownout\n");
        break;
    case HW_IRQ_BATT_BRNOUT:
        INFO("BAT Brownout\n");
        break;
    case 63:
       
        HW_POWER_CTRL.B.DC_OK_IRQ = 0;
        HW_POWER_CTRL.B.VBUSVALID_IRQ = 0;
        HW_POWER_CTRL.B.LINREG_OK_IRQ = 0;
        HW_POWER_CTRL.B.POLARITY_LINREG_OK = 0;
        HW_POWER_CTRL.B.POLARITY_PSWITCH = 0;
        HW_POWER_CTRL.B.POLARITY_VBUSVALID = 0;
        HW_POWER_CTRL.B.POLARITY_VDD5V_GT_VDDIO = 0;
        HW_POWER_CTRL.B.VDDA_BO_IRQ = 0;
        HW_POWER_CTRL.B.VDDD_BO_IRQ = 0;
        HW_POWER_CTRL.B.VDDIO_BO_IRQ = 0;
        HW_POWER_CTRL.B.BATT_BO_IRQ = 0;
        INFO("portPowerIRQ\n");
        break;
    }

}


bool portIsVdd5vPresent()
{
    // Set whenever the 5 V rail (USB) is above VDDIO, i.e. the board is externally
    // powered rather than running from the cells.
    return HW_POWER_STS.B.VDD5V_GT_VDDIO ? true : false;
}


void portBoardPowerOff()
{

    //HW_POWER_VDDIOCTRL.B.DISABLE_FET = 0;
    //HW_POWER_5VCTRL.B.ENABLE_DCDC = 0;

    // Wait for the ON key to be released. ON is also the PMU's PSWITCH, a power-up
    // source: writing PWD while it is still asserted powers the chip down and the PMU
    // powers it straight back up, which presents as a reboot rather than a shutdown.
    // SHIFT+ON reaches here well within the time a key is normally held.
    for (int i = 0; i < 300 && portIsKeyDown(KEY_ON); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(100)); // let PSWITCH debounce settle after release

    // Make sure nothing is armed to wake us again straight away.
    while (HW_RTC_STAT.B.NEW_REGS);
    HW_RTC_PERSISTENT0.B.ALARM_EN = 0;
    while (HW_RTC_STAT.B.NEW_REGS);
    HW_RTC_PERSISTENT0.B.ALARM_WAKE_EN = 0;
    while (HW_RTC_STAT.B.NEW_REGS);
    HW_RTC_PERSISTENT0.B.AUTO_RESTART = 0;
    while (HW_RTC_STAT.B.NEW_REGS);

    printf("PWROFF: pswitch=%d vdd5v=%d on=%d sts=%08lx persistent0=%08lx\n",
           (int)HW_POWER_STS.B.PSWITCH,
           (int)HW_POWER_STS.B.VDD5V_GT_VDDIO,
           (int)portIsKeyDown(KEY_ON),
           (unsigned long)HW_POWER_STS.U,
           (unsigned long)HW_RTC_PERSISTENT0.U);
    vTaskDelay(pdMS_TO_TICKS(400)); // TaskUSBLog polls the log ring every 200 ms; let
                                    // the line out before the rail drops. Must yield:
                                    // portDelayms() spins and would starve that task.

    // The unlock key must accompany PWD in the same 32-bit write. UNLOCK reads back as
    // zero (HW_POWER_RESET reads 00000000 while running), so the original three
    // read-modify-writes of individual fields put UNLOCK=0 into the same word as PWD=1.
    //
    // Disabling the 5 V detect comparators here does NOT help: it kills USB without
    // powering the chip down, leaving it running with a dead USB stack, which looks
    // like a successful shutdown from the host and like a hang from the keyboard.
    HW_POWER_RESET_WR(BF_POWER_RESET_UNLOCK(BV_POWER_RESET_UNLOCK__KEY) | BM_POWER_RESET_PWD);

    for (;;) {
        // Does not return on success.
    }
}


void portPowerInit()
{

    INFO("PWR Init.\n");

    // Power-up context: distinguishes "we were switched on" from "we powered down and
    // something immediately powered us back up", and names the source when it is the
    // latter. Printed every boot so a reboot-after-shutdown leaves evidence.
    printf("PWRUP: pswitch=%d vdd5v=%d sts=%08lx persistent0=%08lx\n",
           (int)HW_POWER_STS.B.PSWITCH,
           (int)HW_POWER_STS.B.VDD5V_GT_VDDIO,
           (unsigned long)HW_POWER_STS.U,
           (unsigned long)HW_RTC_PERSISTENT0.U);

    BF_CLR(POWER_CTRL, CLKGATE);
/*
    BF_SET(POWER_DEBUG, VBUSVALIDPIOLOCK);
    BF_SET(POWER_DEBUG, AVALIDPIOLOCK);
    BF_SET(POWER_DEBUG, BVALIDPIOLOCK);

    BF_SET(POWER_STS, BVALID);
    BF_SET(POWER_STS, AVALID);
    BF_SET(POWER_STS, VBUSVALID);
*/
    //

    portEnableIRQ(HW_IRQ_VDD5V, true);
    portEnableIRQ(HW_IRQ_VDD18_BRNOUT, true);
    portEnableIRQ(HW_IRQ_VDDD_BRNOUT, true);
    portEnableIRQ(HW_IRQ_VDDIO_BRNOUT, true);
    portEnableIRQ(HW_IRQ_BATT_BRNOUT, true);
    portEnableIRQ(63, true);




    double DCDC_VDDD =  1.5L;    //360MHz  1.6
    double DCDC_VDDA = 1.8L;    //360MHz  2.0
    double DCDC_VDDIO = 3.3L; //360MHz  3.6

/*
    double DCDC_VDDD =  1.4L;    //360MHz  1.6
    double DCDC_VDDA = 1.85L;    //360MHz  2.0
    double DCDC_VDDIO = 3.3L; //360MHz  3.6
*/
    HW_POWER_CTRL.B.ENIRQ_DC_OK = 1;
    HW_POWER_CTRL.B.ENIRQ_LINREG_OK = 1;
    HW_POWER_CTRL.B.ENIRQ_VBUS_VALID = 1;
    HW_POWER_CTRL.B.ENIRQ_VDD5V_GT_VDDIO = 1;
    HW_POWER_CTRL.B.ENIRQ_VDDA_BO = 1;
    HW_POWER_CTRL.B.ENIRQ_VDDD_BO = 1;
    HW_POWER_CTRL.B.ENIRQ_VDDIO_BO = 1;
    HW_POWER_CTRL.B.PSWITCH_IRQ_SRC = 1;
    

    
    portGetBatterVoltage_mv();

    //goto fin;
/*
    BF_SET(POWER_DEBUG, VBUSVALIDPIOLOCK);
    BF_SET(POWER_DEBUG, AVALIDPIOLOCK);
    BF_SET(POWER_DEBUG, BVALIDPIOLOCK);

    BF_SET(POWER_STS, BVALID);
    BF_SET(POWER_STS, AVALID);
    BF_SET(POWER_STS, VBUSVALID);*/
    
    //HW_POWER_MINPWR.B.DOUBLE_FETS = 1;
    //HW_POWER_MINPWR.B.HALF_FETS = 1;

/*
    HW_POWER_VDDACTRL.B.ENABLE_LINREG = 1;
    HW_POWER_VDDDCTRL.B.ENABLE_LINREG = 1;

    HW_POWER_VDDACTRL.B.LINREG_OFFSET = 3;
    HW_POWER_VDDDCTRL.B.LINREG_OFFSET = 3;
    HW_POWER_VDDIOCTRL.B.LINREG_OFFSET = 3;

    HW_POWER_VDDDCTRL.B.DISABLE_STEPPING = 1;
    HW_POWER_VDDACTRL.B.DISABLE_STEPPING = 1;
    HW_POWER_VDDIOCTRL.B.DISABLE_STEPPING = 1;*/

    portDelayus(100);
/*
    BF_SET(POWER_LOOPCTRL, TOGGLE_DIF);
    BF_SET(POWER_LOOPCTRL, EN_CM_HYST);
    BF_SET(POWER_LOOPCTRL, EN_DF_HYST);
*/



    BF_WR(POWER_DCLIMITS, POSLIMIT_BOOST, 0xF);
    BF_WR(POWER_DCLIMITS, POSLIMIT_BUCK, 0xF);
    BF_WR(POWER_DCLIMITS, NEGLIMIT, 0x6F);


    //BF_WR(POWER_DCLIMITS, POSLIMIT_BUCK, 0xF);
    //BF_WR(POWER_DCLIMITS, POSLIMIT_BOOST, 0xF);
    //BF_WR(POWER_DCLIMITS, NEGLIMIT, 0x6F);
    


	//BF_SET(POWER_LOOPCTRL, RCSCALE_THRESH);
    //HW_POWER_LOOPCTRL.B.EN_RCSCALE = 3; 

   // BF_SET(POWER_MINPWR, HALF_FETS);
    //BF_SET(POWER_MINPWR, DOUBLE_FETS);

    
    
    //HW_POWER_SPEED.B.CTRL = 3;


    HW_POWER_DCFUNCV.B.VDDD =  (uint16_t)((DCDC_VDDD * DCDC_VDDA)/((DCDC_VDDA - DCDC_VDDD)*6.25e-3));
    HW_POWER_DCFUNCV.B.VDDIO = (uint16_t)((DCDC_VDDIO * DCDC_VDDA)/((DCDC_VDDIO - DCDC_VDDD)*6.25e-3));
    INFO("VDDD VAL:0x%03x, VDDIO VAL:0x%03x\n",HW_POWER_DCFUNCV.B.VDDD, HW_POWER_DCFUNCV.B.VDDIO);

    HW_POWER_5VCTRL.B.EN_BATT_PULLDN = 1;

    HW_POWER_5VCTRL.B.DCDC_XFER = 1;
    HW_POWER_5VCTRL.B.ENABLE_ILIMIT = 0; 
    HW_POWER_LOOPCTRL.B.EN_RCSCALE = 2; 
    HW_POWER_LOOPCTRL.B.DC_C = 0; 


    HW_POWER_5VCTRL.B.OTG_PWRUP_CMPS = 1; //VBUSVALID comparators are enabled !
    HW_POWER_5VCTRL.B.VBUSVALID_5VDETECT = 1;
    HW_POWER_5VCTRL.B.ILIMIT_EQ_ZERO = 0;   //short
    
    HW_POWER_5VCTRL.B.PWDN_5VBRNOUT = 0;
    
    HW_POWER_5VCTRL.B.ENABLE_DCDC = 0;  //Enable DC_DC later

    portDelayus(100);

    HW_POWER_BATTMONITOR.B.BRWNOUT_LVL = 0; // 0.79 V
    HW_POWER_BATTMONITOR.B.BRWNOUT_PWD = 0;
    HW_POWER_BATTMONITOR.B.EN_BATADJ = 1;
    HW_POWER_BATTMONITOR.B.PWDN_BATTBRNOUT = 1;


    HW_POWER_CHARGE.B.ENABLE_FAULT_DETECT = 0;
    //HW_POWER_CHARGE.B.BATTCHRG_I  = 1 << 5;
    //HW_POWER_CHARGE.B.STOP_ILIMIT = 1 << 3;
    HW_POWER_CHARGE.B.USE_EXTERN_R = 1;

    HW_POWER_CHARGE.B.CHRG_STS_OFF = 0;
    HW_POWER_CHARGE.B.PWD_BATTCHRG = 1;


    portDelayus(100);

/*
    HW_POWER_VDDDCTRL.B.ALKALINE_CHARGE = 1;
    HW_POWER_VDDDCTRL.B.DISABLE_FET = 1;
    HW_POWER_VDDDCTRL.B.LINREG_OFFSET = 3;
    HW_POWER_VDDIOCTRL.B.LINREG_OFFSET = 3;
*/


    portDelayus(500);
    
    HW_POWER_VDDDCTRL.B.ENABLE_LINREG = 1;
    HW_POWER_VDDACTRL.B.ENABLE_LINREG = 1;
        

    HW_POWER_VDDDCTRL.B.DISABLE_FET = 1;
    HW_POWER_VDDDCTRL.B.LINREG_OFFSET = 0; 
    HW_POWER_VDDDCTRL.B.ALKALINE_CHARGE = 0;

    


//fin:
   
   
    //HW_POWER_5VCTRL.B.EN_BATT_PULLDN = 1;

    BF_WR(POWER_VDDDCTRL, TRG,  (uint8_t)((1.44 - 0.8)/0.025) );  // val = (TAG_v - 0.8v)/0.025v, 0.8~1.45, 1.2
    portDelayus(200);
    BF_WR(POWER_VDDACTRL, TRG,  (uint8_t)((1.8 - 1.5)/0.025) );  // val = (TAG_v - 1.5v)/0.025v, 1.5~1.95, 1.75
    portDelayus(200);
    BF_WR(POWER_VDDIOCTRL, TRG, (uint8_t)((3.3 - 2.8)/0.025));  // val = (TAG_v - 2.8v)/0.025v, 2.8~3.575, 3.1
    portDelayus(100);

    INFO("VDDIO LDO VAL:0x%03x\n", HW_POWER_VDDIOCTRL.B.TRG );
    INFO("VDDD LDO VAL:0x%03x\n", HW_POWER_VDDDCTRL.B.TRG );
    INFO("VDDA LDO VAL:0x%03x\n", HW_POWER_VDDACTRL.B.TRG );

}