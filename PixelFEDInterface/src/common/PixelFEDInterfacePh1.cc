#include <cassert>
#include <iostream>
#include <iomanip>
#include <time.h>
//#include <unistd.h>

#include "PixelFEDInterface/include/PixelFEDInterfacePh1.h"
#include "PixelUtilities/PixelTestStandUtilities/include/PixelTimer.h"

#include "PixelUtilities/PixelFEDDataTools/include/FIFO3Decoder.h"
#include "PixelUtilities/PixelFEDDataTools/include/ErrorFIFODecoder.h"

using namespace std;

PixelFEDInterfacePh1::PixelFEDInterfacePh1(RegManager* const rm, const std::string& datbase)
  : regManager(rm),
    fitel_fn_base(datbase),
    slink64calls(0)
{
  Printlevel = Printlevel | 16;
  num_SEU.assign(48, 0);
}

PixelFEDInterfacePh1::~PixelFEDInterfacePh1() {
}

int PixelFEDInterfacePh1::setup(const string& fn) {
  setPixelFEDCard(pos::PixelFEDCard(fn));
  return setup();
}

int PixelFEDInterfacePh1::setup(pos::PixelFEDCard c) {
  setPixelFEDCard(c);
  return setup();
}

int PixelFEDInterfacePh1::maybeSwapFitelChannels(int ch) {
  assert(1 <= ch && ch <= 12);
  if (pixelFEDCard.swap_Fitel_order)
    return 12 - ch + 1;
  else
    return ch;
}

std::string PixelFEDInterfacePh1::fitelChannelName(int ch) {
  const int ch2 = maybeSwapFitelChannels(ch);
  char ch_name[16];
  snprintf(ch_name, 16, "Ch%02d_ConfigReg", ch2);
  return std::string(ch_name);
}

int PixelFEDInterfacePh1::setup() {
  // Could possibly want to set different registers for different FMC?
  // And just load the regmaps for both fmc0 and 1 even if we don't have e.g. the upper one
  fRegMapFilename[FMC0_Fitel0] = fitel_fn_base + "/FMCFITEL.txt";
  fRegMapFilename[FMC0_Fitel1] = fitel_fn_base + "/FMCFITEL.txt";
  fRegMapFilename[FMC1_Fitel0] = fitel_fn_base + "/FMCFITEL.txt";
  fRegMapFilename[FMC1_Fitel1] = fitel_fn_base + "/FMCFITEL.txt";

  LoadFitelRegMap(0, 0);
  LoadFitelRegMap(0, 1);
  LoadFitelRegMap(1, 0);
  LoadFitelRegMap(1, 1);

  // Enable the channels that are specified in the control register,
  // same scheme as old FED: channel enabled if bit is 0, channel 1 =
  // LSB.
  //
  // By default, the Fitel config is set to 2 in the reg map = digital
  // power down.  8 means enabled. So go through and turn on the
  // fibers that have a channel enabled.
  std::cout << "cntrl_utca is 0x" << std::hex << pixelFEDCard.cntrl_utca << dec << std::endl;
  assert(pixelFEDCard.which_FMC == 0 || pixelFEDCard.which_FMC == 1);
  for (int channel = 0; channel < 48; ++channel) {
    if (!(pixelFEDCard.cntrl_utca & (1ULL << channel))) {
      const int which_Fitel = channel < 24;
      const int which_map = FitelMapNum(pixelFEDCard.which_FMC, which_Fitel);
      const std::string ch_name = fitelChannelName(channel % 24 / 2 + 1);
      std::cout << "Fitel " << which_Fitel << " map " << which_map << " " << ch_name << " -> 0x08" << std::endl;
      fRegMap[which_map][ch_name].fValue = 0x08;
    }
  }

  const uint32_t tbm_mask_1((pixelFEDCard.cntrl_utca_override ? pixelFEDCard.cntrl_utca_original : pixelFEDCard.cntrl_utca) & 0xFFFFFFFFULL);
  const uint32_t tbm_mask_2(((pixelFEDCard.cntrl_utca_override ? pixelFEDCard.cntrl_utca_original : pixelFEDCard.cntrl_utca) >> 32) & 0xFFFF);
  std::cout << "TBM mask 1: 0x" << std::hex << tbm_mask_1 << "  2: 0x" << tbm_mask_2 << std::dec << std::endl;

  // JMTBAD any and all of these that are hardcoded could be moved to the fedcard...
  std::vector<std::pair<std::string, uint32_t> > cVecReg = {
    {"pixfed_ctrl_regs.PC_CONFIG_OK", 0},
    {"pixfed_ctrl_regs.DDR0_end_readout", 0},
    {"pixfed_ctrl_regs.DDR1_end_readout", 0},
    {"pixfed_ctrl_regs.acq_ctrl.acq_mode", 2},  // 1: TBM fifo, 2: Slink FIFO, 4: FEROL
    {"pixfed_ctrl_regs.acq_ctrl.calib_mode", 1}, 
    {"pixfed_ctrl_regs.acq_ctrl.calib_mode_NEvents", 0}, // = nevents - 1
    {"pixfed_ctrl_regs.data_type", 0}, // 0: real data, 1: constants after TBM fifo, 2: pattern before TBM fifo
    {"pixfed_ctrl_regs.fitel_i2c_cmd_reset", 1}, // fitel I2C bus reset & fifo TX & RX reset
    {"pixfed_ctrl_regs.PACKET_NB", pixelFEDCard.PACKET_NB}, // the FW needs to be aware of the true 32 bit workd Block size for some reason! This is the Packet_nb_true in the python script?!
    {"ctrl.ttc_xpoint_A_out3", 0}, // Used to set the CLK input to the TTC clock from the BP - 3 is XTAL, 0 is BP
    {"ctrl.mgt_xpoint_out1", 3}, // 2 to have 156 MHz clock (obligatory for 10G sling), 3 for 125Mhz clock for 5g on SLink
    {"pixfed_ctrl_regs.TBM_MASK_1", tbm_mask_1},
    {"pixfed_ctrl_regs.TBM_MASK_2", tbm_mask_2},
    {"pixfed_ctrl_regs.tbm_trailer_status_mask", 0x0},
    {"pixfed_ctrl_regs.slink_ctrl.privateEvtNb", 0x01},
    {"pixfed_ctrl_regs.slink_ctrl.slinkFormatH_source_id", pixelFEDCard.fedNumber},
    {"pixfed_ctrl_regs.slink_ctrl.slinkFormatH_FOV", 0xe},
    {"pixfed_ctrl_regs.slink_ctrl.slinkFormatH_Evt_ty", 0},
    {"pixfed_ctrl_regs.slink_ctrl.slinkFormatH_Evt_stat", 0xf},
    {"pixfed_ctrl_regs.tts.timeout_check_valid", pixelFEDCard.timeout_checking_enabled},
    {"pixfed_ctrl_regs.tts.timeout_value", pixelFEDCard.timeout_counter_start},
    {"pixfed_ctrl_regs.tts.timeout_nb_oos_thresh", pixelFEDCard.timeout_number_oos_threshold},
    {"pixfed_ctrl_regs.tts.event_cnt_check_valid", 0},
    {"pixfed_ctrl_regs.tts.evt_err_nb_oos_thresh", 255},
    {"pixfed_ctrl_regs.tts.bc0_ec0_polar", 0},
    {"pixfed_ctrl_regs.tts.force_RDY", 1},
    {"fe_ctrl_regs.fifo_config.overflow_value", 0x700e0}, // set 192val
    {"fe_ctrl_regs.fifo_config.TBM_old_new", 0x1}, // 0x1 = PSI46dig, 0x0 = PROC600 // JMTBAD this needs to be a per channel thing if the bpix boards distribute the layer-1 occupancy
    {"fe_ctrl_regs.fifo_config.channel_of_interest", pixelFEDCard.TransScopeCh},
    {"fe_ctrl_regs.decode_reg_reset", 1}, // init FE spy fifos etc JMTBAD take out if this doesn't work any more
    {"REGMGR_DISPATCH", 1000}, // JMTBAD there were two separate WriteStackReg calls, take this out if it doesn't matter, the 1000 means sleep 1 ms
    {"pixfed_ctrl_regs.fitel_i2c_cmd_reset", 0},
    {"pixfed_ctrl_regs.fitel_rx_i2c_req", 0},
    {"pixfed_ctrl_regs.PC_CONFIG_OK", 1},
  };

  regManager->WriteStackReg(cVecReg);

  usleep(200000);

  int cDDR3calibrated = regManager->ReadReg("pixfed_stat_regs.ddr3_init_calib_done") & 1;

  if (pixelFEDCard.which_FMC == 0) {
    ConfigureFitel(0, 0, true);
    ConfigureFitel(0, 1, true);
  }
  else if (pixelFEDCard.which_FMC == 1) {
    ConfigureFitel(1, 0, true);
    ConfigureFitel(1, 1, true);
  }

  fNthAcq = 0;

  getBoardInfo();

  if ((pixelFEDCard.cntrl_utca & 0xffffffffffffULL) != 0xffffffffffffULL)
    readPhases(true, true);

  regManager->WriteReg ("pixfed_ctrl_regs.slink_core_gtx_reset", 1);
  usleep(10000);
  regManager->WriteReg ("pixfed_ctrl_regs.slink_core_sys_reset", 1);
  usleep(10000);
  regManager->WriteReg ("pixfed_ctrl_regs.slink_core_gtx_reset", 0);
  usleep(10000);
  regManager->WriteReg ("pixfed_ctrl_regs.slink_core_sys_reset", 0);
  std::cout << "Slink status after configure:" << std::endl;
  PrintSlinkStatus();

  printTTSState();

  return cDDR3calibrated;
}

void PixelFEDInterfacePh1::loadFPGA() {
}

void PixelFEDInterfacePh1::getBoardInfo()
{
  std::cout << std::endl << "Board info for FED#" << pixelFEDCard.fedNumber << "\nBoard Type: " << regManager->ReadRegAsString("board_id") << std::endl;
    std::cout << "Revision id: " << regManager->ReadRegAsString("rev_id") << std::endl;
    std::cout << "Firmware id: " << std::hex << regManager->ReadReg("firmware_id") << std::dec << " : " << regManager->ReadRegAsString("firmware_id") << std::endl;
    std::cout << "MAC & IP Source: " << regManager->ReadReg("mac_ip_source") << std::endl;

    std::cout << "MAC Address: " << std::hex
	      << regManager->ReadReg("mac_b5") << ":" 
	      << regManager->ReadReg("mac_b4") << ":"
	      << regManager->ReadReg("mac_b3") << ":"
	      << regManager->ReadReg("mac_b2") << ":"
	      << regManager->ReadReg("mac_b1") << ":"
	      << regManager->ReadReg("mac_b0") << std::dec << std::endl;

    std::cout << "Board Use: " << regManager->ReadRegAsString("pixfed_stat_regs.user_ascii_code_01to04") << regManager->ReadRegAsString("pixfed_stat_regs.user_ascii_code_05to08") << std::endl;

    std::cout << "FW version IPHC : "
	      << regManager->ReadReg("pixfed_stat_regs.user_iphc_fw_id.fw_ver_nb") << "." << regManager->ReadReg("pixfed_stat_regs.user_iphc_fw_id.archi_ver_nb")
	      << "; Date: "
	      << regManager->ReadReg("pixfed_stat_regs.user_iphc_fw_id.fw_ver_day") << "."
	      << regManager->ReadReg("pixfed_stat_regs.user_iphc_fw_id.fw_ver_month") << "."
	      << regManager->ReadReg("pixfed_stat_regs.user_iphc_fw_id.fw_ver_year") <<  std::endl;

    std::cout << "FW version HEPHY : "
	      << regManager->ReadReg("pixfed_stat_regs.user_hephy_fw_id.fw_ver_nb") << "."
	      << regManager->ReadReg("pixfed_stat_regs.user_hephy_fw_id.archi_ver_nb")
	      << "; Date: "
	      << regManager->ReadReg("pixfed_stat_regs.user_hephy_fw_id.fw_ver_day") << "."
	      << regManager->ReadReg("pixfed_stat_regs.user_hephy_fw_id.fw_ver_month") << "."
	      << regManager->ReadReg("pixfed_stat_regs.user_hephy_fw_id.fw_ver_year") << std::endl;

    std::cout << "FMC 8 Present : " << regManager->ReadReg("status.fmc_l8_present") << std::endl;
    std::cout << "FMC 12 Present : " << regManager->ReadReg("status.fmc_l12_present") << std::endl << std::endl;
}


void PixelFEDInterfacePh1::disableFMCs()
{
    std::vector< std::pair<std::string, uint32_t> > cVecReg;
    cVecReg.push_back( { "fmc_pg_c2m", 0 } );
    cVecReg.push_back( { "fmc_l12_pwr_en", 0 } );
    cVecReg.push_back( { "fmc_l8_pwr_en", 0 } );
    regManager->WriteStackReg(cVecReg);
}

void PixelFEDInterfacePh1::enableFMCs()
{
    std::vector< std::pair<std::string, uint32_t> > cVecReg;
    cVecReg.push_back( { "fmc_pg_c2m", 1 } );
    cVecReg.push_back( { "fmc_l12_pwr_en", 1 } );
    cVecReg.push_back( { "fmc_l8_pwr_en", 1 } );
    regManager->WriteStackReg(cVecReg);
}

/*
void PixelFEDInterfacePh1::FlashProm( const std::string & strConfig, const char* pstrFile )
{
    checkIfUploading();

    fpgaConfig->runUpload( strConfig, pstrFile );
}

void PixelFEDInterfacePh1::JumpToFpgaConfig( const std::string & strConfig )
{
    checkIfUploading();

    fpgaConfig->jumpToImage( strConfig );
}

std::vector<std::string> PixelFEDInterfacePh1::getFpgaConfigList()
{
    checkIfUploading();
    return fpgaConfig->getFirmwareImageNames( );
}

void PixelFEDInterfacePh1::DeleteFpgaConfig( const std::string & strId )
{
    checkIfUploading();
    fpgaConfig->deleteFirmwareImage( strId );
}

void PixelFEDInterfacePh1::DownloadFpgaConfig( const std::string& strConfig, const std::string& strDest)
{
    checkIfUploading();
    fpgaConfig->runDownload( strConfig, strDest.c_str());
}

void PixelFEDInterfacePh1::checkIfUploading()
{
    if ( fpgaConfig && fpgaConfig->getUploadingFpga() > 0 )
        throw Exception( "This board is uploading an FPGA configuration" );

    if ( !fpgaConfig )
        fpgaConfig = new CtaFpgaConfig( this );
}
*/

PixelFEDInterfacePh1::FitelRegItem& PixelFEDInterfacePh1::GetFitelRegItem(const std::string& node, int cFMCId, int cFitelId) {
  FitelRegMap::iterator i = fRegMap[FitelMapNum(cFMCId, cFitelId)].find( node );
  assert(i != fRegMap[FitelMapNum(cFMCId, cFitelId)].end());
  return i->second;
}

void PixelFEDInterfacePh1::LoadFitelRegMap(int cFMCId, int cFitelId) {
  const std::string& filename = fRegMapFilename[FitelMapNum(cFMCId, cFitelId)];
  std::ifstream file( filename.c_str(), std::ios::in );
  if (!file) {
    std::cerr << "The Fitel Settings File " << filename << " could not be opened!" << std::endl;
    assert(0);
  }

  std::string line, fName, fAddress_str, fDefValue_str, fValue_str, fPermission_str;
  FitelRegItem fRegItem;

  while (getline(file, line)) {
    if (line.find_first_not_of(" \t") == std::string::npos) continue;
    if (line[0] == '#' || line[0] == '*') continue;
    std::istringstream input(line);
    input >> fName >> fAddress_str >> fDefValue_str >> fValue_str >> fPermission_str;

    fRegItem.fAddress	 = strtoul(fAddress_str.c_str(),  0, 16);
    fRegItem.fDefValue	 = strtoul(fDefValue_str.c_str(), 0, 16);
    fRegItem.fValue	 = strtoul(fValue_str.c_str(),    0, 16);
    fRegItem.fPermission = fPermission_str[0];

    //std::cout << "LoadFitelRegMap(" << cFMCId << "," << cFitelId << "): " << fName << " Addr: 0x" << std::hex << +fRegItem.fAddress << " DefVal: 0x" << +fRegItem.fDefValue << " Val: 0x" << +fRegItem.fValue << std::dec << std::endl;
    fRegMap[FitelMapNum(cFMCId, cFitelId)][fName] = fRegItem;
  }

  file.close();
}

void PixelFEDInterfacePh1::EncodeFitelReg( const FitelRegItem& pRegItem, uint8_t pFMCId, uint8_t pFitelId , std::vector<uint32_t>& pVecReq ) {
  pVecReq.push_back(  pFMCId  << 24 |  pFitelId << 20 |  pRegItem.fAddress << 8 | pRegItem.fValue );
}

void PixelFEDInterfacePh1::DecodeFitelReg( FitelRegItem& pRegItem, uint8_t pFMCId, uint8_t pFitelId, uint32_t pWord ) {
  //uint8_t cFMCId = ( pWord & 0xff000000 ) >> 24;
  //cFitelId = (  pWord & 0x00f00000   ) >> 20;
  pRegItem.fAddress = ( pWord & 0x0000ff00 ) >> 8;
  pRegItem.fValue = pWord & 0x000000ff;
}

void PixelFEDInterfacePh1::i2cRelease(uint32_t pTries)
{
    uint32_t cCounter = 0;
    // release
    regManager->WriteReg("pixfed_ctrl_regs.fitel_rx_i2c_req", 0);
    while (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") != 0)
    {
        if (cCounter > pTries)
        {
            std::cout << "Error, exceeded maximum number of tries for I2C release!" << std::endl;
            break;
        }
        else
        {
            usleep(100);
            cCounter++;
        }
    }
}

bool PixelFEDInterfacePh1::polli2cAcknowledge(uint32_t pTries)
{
    bool cSuccess = false;
    uint32_t cCounter = 0;
    // wait for command acknowledge
    while (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 0)
    {
        if (cCounter > pTries)
        {
            std::cout << "Error, polling for I2C command acknowledge timed out!" << std::endl;
            break;

        }
        else
        {
            usleep(100);
            cCounter++;
        }
    }

    // check the value of that register
    if (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 1)
    {
        cSuccess = true;
    }
    else if (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 3)
    {
        cSuccess = false;
    }
    return cSuccess;
}

bool PixelFEDInterfacePh1::WriteFitelReg (const std::string& pRegNode, int cFMCId, int cFitelId,
                                          uint8_t pValue, bool pVerifLoop) {

  // warning: this does not change the fRegMap items!

  FitelRegItem cRegItem = GetFitelRegItem(pRegNode, cFMCId, cFitelId);
    std::vector<uint32_t> cVecWrite;
    std::vector<uint32_t> cVecRead;

    cRegItem.fValue = pValue;

    EncodeFitelReg ( cRegItem, cFMCId, cFitelId, cVecWrite );

    WriteFitelBlockReg ( cVecWrite );

#ifdef COUNT_FLAG
    fRegisterCount++;
    fTransactionCount++;
#endif

    if ( pVerifLoop )
    {
        cRegItem.fValue = 0;

        EncodeFitelReg ( cRegItem, cFMCId, cFitelId, cVecRead );

        ReadFitelBlockReg ( cVecRead );

        bool cAllgood = false;

        if ( cVecWrite != cVecRead )
        {
            int cIterationCounter = 1;

            while ( !cAllgood )
            {
                if ( cAllgood ) break;

                std::vector<uint32_t> cWrite_again;
                std::vector<uint32_t> cRead_again;

                FitelRegItem cReadItem;
                FitelRegItem cWriteItem;
                DecodeFitelReg ( cWriteItem, cFMCId, cFitelId, cVecWrite.at ( 0 ) );
                DecodeFitelReg ( cReadItem, cFMCId, cFitelId, cVecRead.at ( 0 ) );
                // pFitel->setReg( pRegNode, cReadItem.fValue );

                if ( cIterationCounter == 5 )
                {
                    std::cout << "ERROR !!!\nReadback Value still different after 5 iterations for Register : " << pRegNode << "\n" << std::hex << "Written Value : 0x" << +pValue << "\nReadback Value : 0x" << int ( cRegItem.fValue ) << std::dec << std::endl;
                    std::cout << "Register Adress : " << int ( cRegItem.fAddress ) << std::endl;
                    std::cout << "Fitel Id : " << +cFitelId << std::endl << std::endl;
                    std::cout << "Failed to write register in " << cIterationCounter << " trys! Giving up!" << std::endl;
                }

                EncodeFitelReg ( cWriteItem, cFMCId, cFitelId, cWrite_again );
                cReadItem.fValue = 0;
                EncodeFitelReg ( cReadItem, cFMCId, cFitelId, cRead_again );

                WriteFitelBlockReg ( cWrite_again );
                ReadFitelBlockReg ( cRead_again );

                if ( cWrite_again != cRead_again )
                {
                    if ( cIterationCounter == 5 ) break;

                    cVecWrite.clear();
                    cVecWrite = cWrite_again;
                    cVecRead.clear();
                    cVecRead = cRead_again;
                    cIterationCounter++;
                }
                else
                {
                    std::cout << "Managed to write register correctly in " << cIterationCounter << " Iteration(s)!" << std::endl;
                    cAllgood = true;
                }
            }
        }
        else cAllgood = true;

        if ( cAllgood ) return true;
        else return false;
    }
    else return true;
}

bool PixelFEDInterfacePh1::WriteFitelBlockReg(std::vector<uint32_t>& pVecReq) {
  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_addr", 0x4d);
  bool cSuccess = false;
  // write the encoded registers in the tx fifo
  regManager->WriteBlockReg("fitel_config_fifo_tx", pVecReq);
  // sent an I2C write request
  regManager->WriteReg("pixfed_ctrl_regs.fitel_rx_i2c_req", 1);

  // wait for command acknowledge
  while (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 0) usleep(100);

  uint32_t cVal = regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack");
  //if (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 1)
  if (cVal == 1)
    {
      cSuccess = true;
    }
  //else if (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 3)
  else if (cVal == 3)
    {
      std::cout << "Error writing Registers!" << std::endl;
      cSuccess = false;
    }

  // release
  i2cRelease(10);
  return cSuccess;
}

bool PixelFEDInterfacePh1::ReadFitelBlockReg(std::vector<uint32_t>& pVecReq)
{
  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_addr", 0x4d);
  bool cSuccess = false;
  //uint32_t cVecSize = pVecReq.size();

  // write the encoded registers in the tx fifo
  regManager->WriteBlockReg("fitel_config_fifo_tx", pVecReq);
  // sent an I2C write request
  regManager->WriteReg("pixfed_ctrl_regs.fitel_rx_i2c_req", 3);

  // wait for command acknowledge
  while (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 0) usleep(100);

  uint32_t cVal = regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack");
  //if (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 1)
  if (cVal == 1)
    {
      cSuccess = true;
    }
  //else if (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 3)
  else if (cVal == 3)
    {
      cSuccess = false;
      std::cout << "Error reading registers!" << std::endl;
    }

  // release
  i2cRelease(10);

  // clear the vector & read the data from the fifo
  pVecReq = regManager->ReadBlockRegValue("fitel_config_fifo_rx", pVecReq.size());
  return cSuccess;
}

void PixelFEDInterfacePh1::ConfigureFitel(int cFMCId, int cFitelId , bool pVerifLoop )
{
  FitelRegMap& cFitelRegMap = fRegMap[FitelMapNum(cFMCId, cFitelId)];
    FitelRegMap::iterator cIt = cFitelRegMap.begin();

    while ( cIt != cFitelRegMap.end() )
    {
        std::vector<uint32_t> cVecWrite;
        std::vector<uint32_t> cVecRead;

        uint32_t cCounter = 0;

        for ( ; cIt != cFitelRegMap.end(); cIt++ )
        {
            if (cIt->second.fPermission == 'w')
            {
                EncodeFitelReg( cIt->second, cFMCId, cFitelId, cVecWrite );

                if ( pVerifLoop )
                {
                    FitelRegItem cItem = cIt->second;
                    cItem.fValue = 0;

                    EncodeFitelReg( cItem, cFMCId, cFitelId, cVecRead );
                }
#ifdef COUNT_FLAG
                fRegisterCount++;
#endif
                cCounter++;

            }
        }

        WriteFitelBlockReg(  cVecWrite );
        //usleep(20000);
#ifdef COUNT_FLAG
        fTransactionCount++;
#endif
        if ( pVerifLoop )
        {
            ReadFitelBlockReg( cVecRead );

            // only if I have a mismatch will i decode word by word and compare
            if ( cVecWrite != cVecRead )
            {
                bool cAllgood = false;
                int cIterationCounter = 1;
                while ( !cAllgood )
                {
                    if ( cAllgood ) break;

                    std::vector<uint32_t> cWrite_again;
                    std::vector<uint32_t> cRead_again;

                    auto cMismatchWord = std::mismatch( cVecWrite.begin(), cVecWrite.end(), cVecRead.begin() );

                    while ( cMismatchWord.first != cVecWrite.end() )
                    {


                        FitelRegItem cRegItemWrite;
                        DecodeFitelReg( cRegItemWrite, cFMCId, cFitelId, *cMismatchWord.first );
                        FitelRegItem cRegItemRead;
                        DecodeFitelReg( cRegItemRead, cFMCId, cFitelId, *cMismatchWord.second );

                        if ( cIterationCounter == 5 )
                        {
                            std::cout << "\nERROR !!!\nReadback value not the same after 5 Iteration for Register @ Address: 0x" << std::hex << int( cRegItemWrite.fAddress ) << "\n" << "Written Value : 0x" << int( cRegItemWrite.fValue ) << "\nReadback Value : 0x" << int( cRegItemRead.fValue ) << std::dec << std::endl;
                            std::cout << "Fitel Id : " << int( cFitelId ) << std::endl << std::endl;
                            std::cout << "Failed to write register in " << cIterationCounter << " trys! Giving up!" << std::endl;
                            std::cout << "---<-FMC<-fi---------<-a-----<-v" << std::endl;
                            std::cout << static_cast<std::bitset<32> >( *cMismatchWord.first ) << std::endl << static_cast<std::bitset<32> >( *cMismatchWord.second ) << std::endl << std::endl;
                        }

                        cMismatchWord = std::mismatch( ++cMismatchWord.first, cVecWrite.end(), ++cMismatchWord.second );

                        EncodeFitelReg( cRegItemWrite, cFMCId, cFitelId, cWrite_again );
                        cRegItemRead.fValue = 0;
                        EncodeFitelReg( cRegItemRead, cFMCId, cFitelId, cRead_again );
                    }

                    WriteFitelBlockReg( cWrite_again );
                    ReadFitelBlockReg( cRead_again );

                    if ( cWrite_again != cRead_again )
                    {
                        if ( cIterationCounter == 5 )
                        {
                            std::cout << "Failed to configure FITEL in " << cIterationCounter << " Iterations!" << std::endl;
                            break;
                        }
                        cVecWrite.clear();
                        cVecWrite = cWrite_again;
                        cVecRead.clear();
                        cVecRead = cRead_again;
                        cIterationCounter++;
                    }
                    else
                    {
                      //                        std::cout << "Managed to write all registers correctly in " << cIterationCounter << " Iteration(s)!" << std::endl;
                        cAllgood = true;
                    }
                }
            }
        }
    }
}

std::pair<bool, std::vector<double> > PixelFEDInterfacePh1::ReadADC(int channel, const uint8_t pFMCId, const uint8_t pFitelId, const bool verbose) {
  channel = channel % 12 + 1;

  std::cout << std::endl << "Reading ADC Values on FMC " << +pFMCId << " Fitel " << +pFitelId << " Channel " << channel << std::endl;

  const std::string ch_name = fitelChannelName(channel);
  const uint8_t old_value = fRegMap[FitelMapNum(pFMCId, pFitelId)][ch_name].fValue;
  assert(old_value == 0x2 || old_value == 0x8);
  WriteFitelReg(ch_name, pFMCId, pFitelId, 0xc, true); // to be set back to what it was in the reg map!
  sleep(1); // don't try to 0.5 this thing...

  // the Fitel FMC needs to be set up to be able to read the RSSI on a given Channel:
  // I2C register 0x1: set to 0x4 for RSSI, set to 0x5 for Die Temperature of the Fitel
  // Channel Control Registers: set to 0x02 to disable RSSI for this channel, set to 0x0c to enable RSSI for this channel
  // the ADC always reads the sum of all the enabled channels!
  //initial FW setup
  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_cmd_reset", 1);
  sleep(1);
  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_cmd_reset", 0);

  std::vector<std::pair<std::string, uint32_t> > cVecReg;
  cVecReg.push_back({"pixfed_ctrl_regs.fitel_rx_i2c_req", 0});
  cVecReg.push_back({"pixfed_ctrl_regs.fitel_i2c_addr", 0x77});

  regManager->WriteStackReg(cVecReg);

  //first, write the correct registers to configure the ADC
  //the values are: Address 0x01 -> 0x1<<6 & 0x1f
  //                Address 0x02 -> 0x1

  // Vectors for write and read data!
  std::vector<uint32_t> cVecWrite;
  std::vector<uint32_t> cVecRead;

  //encode them in a 32 bit word and write, no readback yet
  cVecWrite.push_back(  pFMCId  << 24 |  pFitelId << 20 |  0x1 << 8 | 0x5f );
  cVecWrite.push_back(  pFMCId  << 24 |  pFitelId << 20 |  0x2 << 8 | 0x01 );
  regManager->WriteBlockReg("fitel_config_fifo_tx", cVecWrite);

  // sent an I2C write request
  regManager->WriteReg("pixfed_ctrl_regs.fitel_rx_i2c_req", 1);

  // wait for command acknowledge
  while(regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 0) usleep(100);

  uint32_t cVal = regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack");
  bool success = cVal != 3;
  if (!success) std::cout << "Error reading registers!" << std::endl;

  // release
  i2cRelease(10);
  sleep(2);

  //now prepare the read-back of the values
  uint8_t cNWord = 10;

  for (uint8_t cIndex = 0; cIndex < cNWord; cIndex++)
      cVecRead.push_back( pFMCId << 24 | pFitelId << 20 | (0x6 + cIndex ) << 8 | 0 );

  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_addr", 0x4c);

  regManager->WriteBlockReg( "fitel_config_fifo_tx", cVecRead );
  // sent an I2C write request
  regManager->WriteReg("pixfed_ctrl_regs.fitel_rx_i2c_req", 3);

  // wait for command acknowledge
  while (regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack") == 0) usleep(100);

  cVal = regManager->ReadReg("pixfed_stat_regs.fitel_i2c_ack");
  if (cVal == 3)
    std::cout << "Error reading registers!" << std::endl;
  success = success && cVal != 3;

  cVecRead = regManager->ReadBlockRegValue("fitel_config_fifo_rx", cNWord);

  // release
  i2cRelease(10);
  usleep(500);

  // now convert to Voltages!
  std::vector<double> cLTCValues(cNWord / 2, 0);

  double cConstant = 0.00030518;
  // each value is hidden in 2 I2C words
  for (int cMeasurement = 0; cMeasurement < cNWord / 2; cMeasurement++) {
    // build the values
    uint16_t cValue = ((cVecRead.at(2 * cMeasurement) & 0x7F) << 8) + (cVecRead.at(2 * cMeasurement + 1) & 0xFF);
    uint8_t cSign = (cValue >> 14) & 0x1;

    //now the conversions are different for each of the voltages, so check by cMeasurement
    if (cMeasurement == 4)
      cLTCValues.at(cMeasurement) = (cSign == 0b1) ? (-( 32768 - cValue ) * cConstant + 2.5) : (cValue * cConstant + 2.5);

    else
      cLTCValues.at(cMeasurement) = (cSign == 0b1) ? (-( 32768 - cValue ) * cConstant) : (cValue * cConstant);

    if (verbose)
      std::cout << "V" << cMeasurement + 1 << " = " << std::setw(15) << cLTCValues.at(cMeasurement) << " ";
  }

  // now I have all 4 voltage values in a vector of size 5
  // V1 = cLTCValues[0]
  // V2 = cLTCValues[1]
  // V3 = cLTCValues[2]
  // V4 = cLTCValues[3]
  // Vcc = cLTCValues[4]
  //
  // the RSSI value = fabs(V3-V4) / R=150 Ohm [in Amps]
  if (verbose) {
    const double cADCVal = fabs(cLTCValues.at(2) - cLTCValues.at(3)) / 150.0;
    std::cout << " RSSI " << cADCVal * 1000  << " mA" << std::endl;
  }

  WriteFitelReg(ch_name, pFMCId, pFitelId, old_value, true);

  return std::make_pair(success, cLTCValues);
}


int PixelFEDInterfacePh1::reset() {
  // JMTBAD from fedcard

  //  setup(); // JMTBAD

  return 0;
}

void PixelFEDInterfacePh1::resetFED() {
}

void PixelFEDInterfacePh1::sendResets(unsigned which) {
}

void PixelFEDInterfacePh1::armOSDFifo(int channel, int rochi, int roclo) {
}

uint32_t PixelFEDInterfacePh1::readOSDFifo(int channel) {
  return 0;
}

void prettyprintPhase(const std::vector<uint32_t>& pData, int pChannel) {
  int channelToPrint = pChannel + 1;
  int index = pChannel * 4;
  if (pChannel < 0) {
    channelToPrint = -pChannel;
    index = 0;
  }
  
  std::cout << "Fibre: " << std::setw(2) <<  channelToPrint << "    " <<
              std::bitset<1>( (pData.at( (index ) + 0 ) >> 10 ) & 0x1 )   << "    " << std::setw(2) <<
              ((pData.at( (index ) + 0 ) >> 5  ) & 0x1f )  << "    " << std::setw(2) <<
              ((pData.at( (index ) + 0 )       ) & 0x1f ) << "    " <<
              std::bitset<32>( pData.at( (index ) + 1 )) << "    " <<
              std::bitset<1>( (pData.at( (index ) + 2 ) >> 31 ) & 0x1 )  << " " << std::setw(2) <<
              ((pData.at( (index ) + 2 ) >> 23 ) & 0x1f)  << " " << std::setw(2) <<
              ((pData.at( (index ) + 2 ) >> 18 ) & 0x1f)  << " " << std::setw(2) <<
              ((pData.at( (index ) + 2 ) >> 13 ) & 0x1f ) << " " << std::setw(2) <<
              ((pData.at( (index ) + 2 ) >> 8  ) & 0x1f ) << " " << std::setw(2) <<
              ((pData.at( (index ) + 2 ) >> 5  ) & 0x7  ) << " " << std::setw(2) <<
              ((pData.at( (index ) + 2 )       ) & 0x1f ) << std::endl;
}

void PixelFEDInterfacePh1::readPhases(bool verbose, bool override_timeout) {
    // Perform all the resets
    std::vector< std::pair<std::string, uint32_t> > cVecReg;
    cVecReg.push_back( { "fe_ctrl_regs.decode_reset", 1 } ); // reset deocode auto clear
    cVecReg.push_back( { "fe_ctrl_regs.decode_reg_reset", 1 } ); // reset REG auto clear
    cVecReg.push_back( { "fe_ctrl_regs.idel_ctrl_reset", 1} );
    regManager->WriteStackReg(cVecReg);
    cVecReg.clear();
    cVecReg.push_back( { "fe_ctrl_regs.idel_ctrl_reset", 0} );
    regManager->WriteStackReg(cVecReg);
    cVecReg.clear();

    // NOTE: here the register idel_individual_ctrl is the base address of the registers for all 48 channels. So each 32-bit word contains the control info for 1 channel. Thus by creating a vector of 48 32-bit words and writing them at the same time I can write to each channel without using relative addresses!

    // set the parameters for IDELAY scan
    std::vector<uint32_t> cValVec;
    for (uint32_t cChannel = 0; cChannel < 48; cChannel++)
        cValVec.push_back( 0x80000000 );
    regManager->WriteBlockReg( "fe_ctrl_regs.idel_individual_ctrl", cValVec );
    cValVec.clear();

    // set auto_delay_scan and set idel_RST
    for (uint32_t cChannel = 0; cChannel < 48; cChannel++)
        cValVec.push_back( 0xc0000000 );
    regManager->WriteBlockReg( "fe_ctrl_regs.idel_individual_ctrl", cValVec );
    cValVec.clear();

    // set auto_delay_scan and remove idel_RST
    for (uint32_t cChannel = 0; cChannel < 48; cChannel++)
        cValVec.push_back( 0x80000000 );
    regManager->WriteBlockReg( "fe_ctrl_regs.idel_individual_ctrl", cValVec );
    cValVec.clear();

    // initialize Phase Finding
    regManager->WriteReg("fe_ctrl_regs.initialize_swap", 0);
    regManager->WriteReg("fe_ctrl_regs.initialize_swap", 1);
    regManager->WriteReg("fe_ctrl_regs.initialize_swap", 0);

    std::cout << "FED# " <<  pixelFEDCard.fedNumber << " Initializing Phase Finding ..." << std::endl << std::endl;
    PixelTimer timer;
    timer.start();
    while (((regManager->ReadBlockRegValue("idel_individual_stat.CH0", 4).at(2) >> 29) & 0x03) != 0x0)
      usleep (1000);
    timer.stop();
    
    std::cout << "FED# " <<  pixelFEDCard.fedNumber << " Time to run the initial phase finding: " << timer.tottime() << "; swapping phases ... " << std::endl;
    timer.reset();
    timer.start();
    while (((regManager->ReadBlockRegValue("idel_individual_stat.CH0", 4).at(2) >> 29) & 0x03) != 0x2)
      usleep (1000);
    timer.stop();
    std::cout << "FED# " <<  pixelFEDCard.fedNumber << " Swap fininshed, additional time: " << timer.tottime() << "; phase finding results: " << std::endl;

    uint32_t cNChannel = 24;
    std::vector<uint32_t> cReadValues = regManager->ReadBlockRegValue ( "idel_individual_stat_block", cNChannel * 4 );
    std::cout << "FIBRE CTRL_RDY CNTVAL_Hi CNTVAL_Lo   pattern:                     S H1 L1 H0 L0   W R" << std::endl;
    for (uint32_t cChannel = 0; cChannel < cNChannel; cChannel++)
      prettyprintPhase(cReadValues, cChannel);

    // JMTBAD this not needed anymore?
//    cVecReg.push_back( { "pixfed_ctrl_regs.PC_CONFIG_OK", 0} );
//    regManager->WriteStackReg(cVecReg);
//    cVecReg.clear();
//    cVecReg.push_back( { "pixfed_ctrl_regs.PC_CONFIG_OK", 1} );
//    regManager->WriteStackReg(cVecReg);
//    cVecReg.clear();
}

uint8_t PixelFEDInterfacePh1::getTTSState() {
  return uint8_t(regManager->ReadReg("pixfed_stat_regs.tts.word") & 0x0000000F);
}

void PixelFEDInterfacePh1::printTTSState() {
  const uint8_t cWord = getTTSState();
  std::cout << "FED#" << pixelFEDCard.fedNumber << " TTS State: ";
  if      (cWord == 8)  std::cout << "RDY";
  else if (cWord == 4)  std::cout << "BSY";
  else if (cWord == 2)  std::cout << "OOS";
  else if (cWord == 1)  std::cout << "OVF";
  else if (cWord == 0)  std::cout << "DIC";
  else if (cWord == 12) std::cout << "ERR";
  std::cout << std::endl;
}

void PixelFEDInterfacePh1::getSFPStatus(uint8_t pFMCId) {
    std::cout << "Initializing SFP+ for SLINK" << std::endl;

    regManager->WriteReg ("pixfed_ctrl_regs.fitel_i2c_cmd_reset", 1);
    sleep (0.5);
    regManager->WriteReg ("pixfed_ctrl_regs.fitel_i2c_cmd_reset", 0);

    std::vector<std::pair<std::string, uint32_t> > cVecReg;
    cVecReg.push_back ({"pixfed_ctrl_regs.fitel_sfp_i2c_req", 0});
    cVecReg.push_back ({"pixfed_ctrl_regs.fitel_rx_i2c_req", 0});

    cVecReg.push_back ({"pixfed_ctrl_regs.fitel_i2c_addr", 0x38});

    regManager->WriteStackReg (cVecReg);

    // Vectors for write and read data!
    std::vector<uint32_t> cVecWrite;
    std::vector<uint32_t> cVecRead;

    //encode them in a 32 bit word and write, no readback yet
    cVecWrite.push_back (  pFMCId  << 24 |   0x00 );
    regManager->WriteBlockReg ("fitel_config_fifo_tx", cVecWrite);


    // sent an I2C write request
    // Edit G. Auzinger
    // write 1 to sfp_i2c_reg to trigger an I2C write transaction - Laurent's BS python script is ambiguous....
    regManager->WriteReg ("pixfed_ctrl_regs.fitel_sfp_i2c_req", 1);

    // wait for command acknowledge
    while (regManager->ReadReg ("pixfed_stat_regs.fitel_i2c_ack") == 0) usleep (100);

    uint32_t cVal = regManager->ReadReg ("pixfed_stat_regs.fitel_i2c_ack");

    if (cVal == 3)
        std::cout << "Error during i2c write!" << cVal << std::endl;

    //release handshake with I2C
    regManager->WriteReg ("pixfed_ctrl_regs.fitel_sfp_i2c_req", 0);

    // wait for command acknowledge
    while (regManager->ReadReg ("pixfed_stat_regs.fitel_i2c_ack") != 0) usleep (100);

    usleep (500);
    //////////////////////////////////////////////////
    ////THIS SHOULD BE THE END OF THE WRITE OPERATION, BUS SHOULD BE IDLE
    /////////////////////////////////////////////////

    //regManager->WriteReg ("pixfed_ctrl_regs.fitel_i2c_cmd_reset", 1);
    //sleep (0.5);
    //regManager->WriteReg ("pixfed_ctrl_regs.fitel_i2c_cmd_reset", 0);

    //std::vector<std::pair<std::string, uint32_t> > cVecReg;
    //cVecReg.push_back ({"pixfed_ctrl_regs.fitel_sfp_i2c_req", 0});
    //cVecReg.push_back ({"pixfed_ctrl_regs.fitel_rx_i2c_req", 0});
    //cVecReg.push_back ({"pixfed_ctrl_regs.fitel_i2c_addr", 0x38});
    //regManager->WriteStackReg (cVecReg);
    //cVecReg.clear();

    //Vector for the reply
    //std::vector<uint32_t> cVecRead;
    cVecRead.push_back ( pFMCId << 24 | 0x00 );
    regManager->WriteBlockReg ("fitel_config_fifo_tx", cVecRead);
    //this issues an I2C read request via FW
    regManager->WriteReg ("pixfed_ctrl_regs.fitel_sfp_i2c_req", 3);

    // wait for command acknowledge
    while (regManager->ReadReg ("pixfed_stat_regs.fitel_i2c_ack") == 0) usleep (100);

    cVal = regManager->ReadReg ("pixfed_stat_regs.fitel_i2c_ack");

    if (cVal == 3)
        std::cout << "Error during i2c write!" << cVal << std::endl;

    cVecRead.clear();
    //only read 1 word
    cVecRead = regManager->ReadBlockRegValue ("fitel_config_fifo_rx", cVecWrite.size() );

    std::cout << "SFP+ Status of FMC " << +pFMCId << std::endl;

    for (size_t i = 0; i < cVecRead.size(); ++i) {
      uint32_t cRead = cVecRead[i];
        cRead = cRead & 0xF;
        std::cout << "-> SFP_MOD_ABS:  "  << (cRead >> 0 & 0x1) << std::endl;
        std::cout << "-> SFP_TX_DIS:   "   << (cRead >> 1 & 0x1) << std::endl;
        std::cout << "-> SFP_TX_FAULT: " << (cRead >> 2 & 0x1) << std::endl;
        std::cout << "-> SFP_RX_LOS:   "  << (cRead >> 3 & 0x1) << std::endl;
    }

    //release handshake with I2C
    regManager->WriteReg ("pixfed_ctrl_regs.fitel_sfp_i2c_req", 0);

    // wait for command acknowledge
    while (regManager->ReadReg ("pixfed_stat_regs.fitel_i2c_ack") != 0) usleep (100);

    printTTSState();
}

void PixelFEDInterfacePh1::PrintSlinkStatus()
{

  printTTSState();

    for (int i = 0; i < 50; i++)
        std::cout << " *";

    std::cout << std::endl;

    //check the link status
    uint8_t sync_loss =  regManager->ReadReg ("pixfed_stat_regs.slink_core_status.sync_loss");
    uint8_t link_down =  regManager->ReadReg ("pixfed_stat_regs.slink_core_status.link_down");
    uint8_t link_full =  regManager->ReadReg ("pixfed_stat_regs.slink_core_status.link_full");
    std::cout << "PixFED SLINK information : ";
    (sync_loss == 1) ? std::cout << "SERDES out of sync" : std::cout << "SERDES OK";
    std::cout << " | ";
    (link_down == 1) ? std::cout << "Link up" : std::cout << "Link down";
    std::cout << " | ";
    (link_full == 1) ? std::cout << "ready for data" : std::cout << "max 16 words left";
    std::cout << std::endl;



    //LINK status
    std::cout << "SLINK core status        : ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x1);
    //    std::cout << "data_31to0 " <<  regManager->ReadReg("pixfed_stat_regs.slink_core_status.data_31to0") << std::endl;
    uint32_t cLinkStatus =  regManager->ReadReg ("pixfed_stat_regs.slink_core_status.data_31to0") ;
    ( (cLinkStatus & 0x80000000) >> 31) ? std::cout << "Test mode |" : std::cout << "FED data  |";
    ( (cLinkStatus & 0x40000000) >> 30) ? std::cout << " Link up |" : std::cout << " Link down |";
    ( (cLinkStatus & 0x20000000) >> 29) ? std::cout << "\0 |"  : std::cout << " Link backpressured |";
    ( (cLinkStatus & 0x10000000) >> 28) ? std::cout << " >= 1 block free for data |" : std::cout << "\0 |";
    ( (cLinkStatus & 0x00000040) >> 6) ? std::cout << " 2 fragments with the same trigger number |" : std::cout << "\0 |";
    ( (cLinkStatus & 0x00000020) >> 5) ? std::cout << " Trailer duplicated |" : std::cout << "\0 |";
    ( (cLinkStatus & 0x00000010) >> 4) ? std::cout << " Header duplicated |" : std::cout << "\0 |";
    ( (cLinkStatus & 0x00000008) >> 3) ? std::cout << "   Between header and trailer |" << std::endl : std::cout << "\0 |";

    if ( (cLinkStatus & 0x00000007) == 1)
        std::cout << "   State machine: idle" << std::endl;
    else if ( (cLinkStatus & 0x00000007) == 2)
        std::cout << "   State machine: Read data from FED" << std::endl;
    else if ( (cLinkStatus & 0x00000007) == 4)
        std::cout << "   State machine: Closing block" << std::endl;
    else
        std::cout << "   State machine: fucked (Return value: " <<  (cLinkStatus & 0x00000007) << ")" << std::endl;


    //Status CORE
    std::cout << "Status CORE: " ;
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x6);
    cLinkStatus = regManager->ReadReg ("pixfed_stat_regs.slink_core_status.data_31to0");
    ( (cLinkStatus & 0x80000000) >> 31) ? std::cout << "   Core initialized" << std::endl : std::cout << "   Core not initialized" << std::endl;

    //Data counter
    std::cout << "Input Counters : ";
    std::cout << "Data counter: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x2);
    uint64_t val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << " | ";

    //Event counter
    std::cout << "Event counter: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x3);
    val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << " | ";

    //Block counter
    std::cout << "Block counter: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x4);
    val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << " | ";

    //Packets recieved counter
    std::cout << "Pckt rcv counter: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x5);
    val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << std::endl;

    //Packets send counter
    std::cout << "Output Counter : ";
    std::cout << "Pckt send counter: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x7);
    val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << " | ";

    //Status build
    std::cout << "Status build: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x8);
    val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << " | ";

    //Back pressure counter
    std::cout << "BackP counter: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0x9);
    val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << std::endl;

    //Version number
    //std::cout << "Version number: ";
    //regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0xa);
    //val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    //std::cout <<  val << std::endl;

    //SERDES status
    //std::cout << "SERDES status: ";
    //regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0xb);
    //val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    //std::cout <<  val << std::endl;

    //Retransmitted packages counter
    std::cout << "Error Counters : ";
    std::cout << "Retrans pkg counter: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0xc);
    val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << " | ";

    //FED CRC error
    std::cout << "FED CRC error counter: ";
    regManager->WriteReg ("pixfed_ctrl_regs.slink_core_status_addr", 0xd);
    val = regManager->ReadRegsAs64("pixfed_stat_regs.slink_core_status.data_63to32", "pixfed_stat_regs.slink_core_status.data_31to0");
    std::cout <<  val << std::endl;

    std::cout << "Data transfer block status: " << std::endl;
    std::cout << "      Block1: ";
    ( (cLinkStatus & 0x00000080) >> 7) ? std::cout << "ready    " : std::cout << "not ready";
    std::cout << " | Block2: ";
    ( (cLinkStatus & 0x00000040) >> 6) ? std::cout << "ready    " : std::cout << "not ready";
    std::cout << " | Block3: ";
    ( (cLinkStatus & 0x00000020) >> 5) ? std::cout << "ready    " : std::cout << "not ready";
    std::cout << " | Block4: ";
    ( (cLinkStatus & 0x00000010) >> 4) ? std::cout << "ready" : std::cout << "not ready" << std::endl;

    std::cout << "Data transfer block usage: " << std::endl;
    std::cout << "      Block1: ";
    ( (cLinkStatus & 0x00000008) >> 3) ? std::cout << "used    " : std::cout << "not used ";
    std::cout << " | Block2: ";
    ( (cLinkStatus & 0x00000004) >> 2) ? std::cout << "used    " : std::cout << "not used ";
    std::cout << " | Block3: ";
    ( (cLinkStatus & 0x00000002) >> 1) ? std::cout << "used    " : std::cout << "not used ";
    std::cout << " | Block4: ";
    ( (cLinkStatus & 0x00000001) >> 0) ? std::cout << "used    " : std::cout << "not used " << std::endl;

    for (int i = 0; i < 50; i++)
        std::cout << " *";

    std::cout << std::endl;

}

void PixelFEDInterfacePh1::prepareCalibrationMode(unsigned nevents) {
  last_calib_mode_nevents = nevents;
  return;

  std::cout << "Requesting " << nevents << " Events from FW!" << std::endl;

  std::vector< std::pair<std::string, uint32_t> > cVecReg = {
    {"pixfed_ctrl_regs.PC_CONFIG_OK", 0},
    {"pixfed_ctrl_regs.acq_ctrl.calib_mode", 1},
    {"pixfed_ctrl_regs.acq_ctrl.calib_mode_NEvents", nevents - 1},
    {"REGMGR_DISPATCH", 1000},
    {"pixfed_ctrl_regs.PC_CONFIG_OK", 1},
  };

  regManager->WriteStackReg(cVecReg);

  std::cout << "Send the trigger now!" << std::endl;
}

std::vector<uint32_t> PixelFEDInterfacePh1::readTransparentFIFO()
{
    std::vector<uint32_t> cFifoVec = regManager->ReadBlockRegValue ( "fifo.bit_stream", 512 );
    //vectors to pass to the NRZI decoder as reference to be filled by that
    std::vector<uint8_t> c5bSymbol, c5bNRZI, c4bNRZI;
    decodeTransparentSymbols (cFifoVec, c5bSymbol, c5bNRZI, c4bNRZI);
    prettyPrintTransparentFIFO (cFifoVec, c5bSymbol, c5bNRZI, c4bNRZI);
    return cFifoVec;
}

int PixelFEDInterfacePh1::drainTransparentFifo(uint32_t* data) {
  const size_t MAX_TRANSPARENT_FIFO = 1024;
  std::vector<uint32_t> v(readTransparentFIFO());
  const int ie(min(v.size(), MAX_TRANSPARENT_FIFO));
  for (int i = 0; i < ie; ++i)
    data[i] = v[i];
  return ie;
}

void PixelFEDInterfacePh1::decodeTransparentSymbols (const std::vector<uint32_t>& pInData, std::vector<uint8_t>& p5bSymbol, std::vector<uint8_t>& p5bNRZI, std::vector<uint8_t>& p4bNRZI)
{
    //first, clear all the non-const references
    p5bSymbol.clear();
    p5bNRZI.clear();
    p4bNRZI.clear();

    //ok, then internally generate a vector of 5b Symbols and use a uint8_t for that
    for (size_t i = 0; i < pInData.size(); ++i)
    {
      uint32_t cWord = pInData[i];
        cWord &= 0x0000001f;

        p5bSymbol.push_back (cWord);

        //next, fill the 5bNRZI 4bNRZI with 0 symbols with 0x1f
        p5bNRZI.push_back (0x1f);
        p4bNRZI.push_back (0);
    }


    for (uint32_t i = 1; i < pInData.size(); i++)
    {

        if (p5bSymbol[i] == 0x1f) p5bNRZI[i] = 0x16; //print 10110

        if (p5bSymbol[i] == 0)    p5bNRZI[i] = 0x16; //print 10110

        //if( i > 0 )
        //{

        if ( (p5bSymbol[i] == 0x14) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x1e; //# print '11110'

        if ( (p5bSymbol[i] == 0x0e) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x09; //# print '01001'

        if ( (p5bSymbol[i] == 0x18) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x14; //# print '10100'

        if ( (p5bSymbol[i] == 0x19) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x15; //# print '10101'

        if ( (p5bSymbol[i] == 0x0c) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x0a; //# print '01010'

        if ( (p5bSymbol[i] == 0x0d) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x0b; //# print '01011'

        if ( (p5bSymbol[i] == 0x0b) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x0e; //# print '01110'

        if ( (p5bSymbol[i] == 0x0a) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x0f; //# print '01111'

        if ( (p5bSymbol[i] == 0x1c) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x12; //# print '10010'

        if ( (p5bSymbol[i] == 0x1d) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x13; //# print '10011'

        if ( (p5bSymbol[i] == 0x1b) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x16; //# print '10110'

        if ( (p5bSymbol[i] == 0x1a) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x17; //# print '10111'

        if ( (p5bSymbol[i] == 0x13) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x1a; //# print '11010'

        if ( (p5bSymbol[i] == 0x12) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x1b; //# print '11011'

        if ( (p5bSymbol[i] == 0x17) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x1c; //# print '11100'

        if ( (p5bSymbol[i] == 0x16) && ( (p5bSymbol[i - 1] & 0x1) == 0) )  p5bNRZI[i] = 0x1d; //# print '11101'

        if ( (p5bSymbol[i] == 0x0b) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x1e; //# print '11110'

        if ( (p5bSymbol[i] == 0x11) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x09; //# print '01001'

        if ( (p5bSymbol[i] == 0x07) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x14; //# print '10100'

        if ( (p5bSymbol[i] == 0x06) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x15; //# print '10101'

        if ( (p5bSymbol[i] == 0x13) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x0a; //# print '01010'

        if ( (p5bSymbol[i] == 0x12) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x0b; //# print '01011'

        if ( (p5bSymbol[i] == 0x14) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x0e; //# print '01110'

        if ( (p5bSymbol[i] == 0x15) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x0f; //# print '01111'

        if ( (p5bSymbol[i] == 0x03) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x12; //# print '10010'

        if ( (p5bSymbol[i] == 0x02) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x13; //# print '10011'

        if ( (p5bSymbol[i] == 0x04) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x16; //# print '10110'

        if ( (p5bSymbol[i] == 0x05) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x17; //# print '10111'

        if ( (p5bSymbol[i] == 0x0c) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x1a; //# print '11010'

        if ( (p5bSymbol[i] == 0x0d) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x1b; //# print '11011'

        if ( (p5bSymbol[i] == 0x08) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x1c; //# print '11100'

        if ( (p5bSymbol[i] == 0x09) && ( (p5bSymbol[i - 1] & 0x1) == 1) )  p5bNRZI[i] = 0x1d; //# print '11101'
    }

    for (uint32_t i = 0; i < pInData.size(); i++)
    {
        if (p5bNRZI[i] == 0x1e)  p4bNRZI[i] = 0x0; // #

        if (p5bNRZI[i] == 0x09)  p4bNRZI[i] = 0x1; // #

        if (p5bNRZI[i] == 0x14)  p4bNRZI[i] = 0x2; // #

        if (p5bNRZI[i] == 0x15)  p4bNRZI[i] = 0x3; // #

        if (p5bNRZI[i] == 0x0a)  p4bNRZI[i] = 0x4; // #

        if (p5bNRZI[i] == 0x0b)  p4bNRZI[i] = 0x5; // #

        if (p5bNRZI[i] == 0x0e)  p4bNRZI[i] = 0x6; // #

        if (p5bNRZI[i] == 0x0f)  p4bNRZI[i] = 0x7; // #

        if (p5bNRZI[i] == 0x12)  p4bNRZI[i] = 0x8; // #

        if (p5bNRZI[i] == 0x13)  p4bNRZI[i] = 0x9; // #

        if (p5bNRZI[i] == 0x16)  p4bNRZI[i] = 0xa; // #

        if (p5bNRZI[i] == 0x17)  p4bNRZI[i] = 0xb; // #

        if (p5bNRZI[i] == 0x1a)  p4bNRZI[i] = 0xc; // #

        if (p5bNRZI[i] == 0x1b)  p4bNRZI[i] = 0xd; // #

        if (p5bNRZI[i] == 0x1c)  p4bNRZI[i] = 0xe; // #

        if (p5bNRZI[i] == 0x1d)  p4bNRZI[i] = 0xf; // #

        if (p5bNRZI[i] == 0x1f)  p4bNRZI[i] = 0x0; // #error
    }
}

void PixelFEDInterfacePh1::prettyPrintTransparentFIFO (const std::vector<uint32_t>& pFifoVec, const std::vector<uint8_t>& p5bSymbol, const std::vector<uint8_t>& p5bNRZI, const std::vector<uint8_t>& p4bNRZI)
{
    std::cout << std::endl <<  "Transparent FIFO: "  << std::endl
              << " Timestamp      5b Symbol " << std::endl
              << "                5b NRZI   " << std::endl
              << "                4b NRZI   " << std::endl
              << "                TBM Core A" << std::endl
              << "                TBM Core B" << std::endl;

    // now print and decode the FIFO data
    for (uint32_t j = 0; j < 32; j++)
    {
        //first line with the timestamp
        std::cout << "          " << ( (pFifoVec.at ( (j * 16) ) >> 20 ) & 0xfff) << ": ";

        for (uint32_t i = 0; i < 16; i++)
            std::cout << " " << std::bitset<5> ( ( (p5bSymbol.at (i + (j * 16) ) >> 0) & 0x1f) );

        std::cout << std::endl << "               ";

        //now the line with the 5bNRZI word
        for (uint32_t i = 0; i < 16; i++)
        {
            if (p5bNRZI.at (i + j * 16) == 0x1f) std::cout << " " << "ERROR";
            else std::cout << " " << std::bitset<5> ( ( (p5bNRZI.at (i + (j * 16) ) >> 0) & 0x1f) );
        }

        std::cout << std::endl << "               ";

        //now the line with the 4bNRZI word
        for (uint32_t i = 0; i < 16; i++)
            std::cout << " " << std::bitset<4> ( ( (p4bNRZI.at (i + (j * 16) ) >> 0) & 0x1f) ) << " ";

        std::cout << std::endl << "               ";

        //now the line with TBM Core A word
        for (uint32_t i = 0; i < 16; i++)
        {
            std::cout << " " << std::bitset<1> ( ( (p4bNRZI.at (i + (j * 16) ) >> 3) & 0x1) );
            std::cout << " " << std::bitset<1> ( ( (p4bNRZI.at (i + (j * 16) ) >> 1) & 0x1) );
        }

        std::cout << std::endl << "               ";

        //now the line with TBM Core A word
        for (uint32_t i = 0; i < 16; i++)
        {
            std::cout << " " << std::bitset<1> ( ( ( (p4bNRZI.at (i + (j * 16) ) >> 2) & 0x1) ^ 0x1) );
            std::cout << " " << std::bitset<1> ( ( ( (p4bNRZI.at (i + (j * 16) ) >> 0) & 0x1) ^ 0x1) );
        }

        std::cout << std::endl << std::endl;
    }
}



void prettyprintSpyFIFO(const std::vector<uint32_t>& pVec) {
  for (size_t i = 0; i < pVec.size(); ++i)    {
    uint32_t cWord = pVec[i];
    const uint32_t timestamp = (cWord >> 20) & 0xfff;
    const uint32_t marker = (cWord & 0xf0) >> 4;
    if ((cWord & 0xff) != 0) std::cout << std::hex << (cWord & 0xff) << std::dec << " " ;
    if (marker == 0xb || marker == 6 || marker == 7 || marker == 0xf)
      std::cout << "       " << timestamp << std::endl;
  }
}

std::vector<uint32_t> PixelFEDInterfacePh1::readSpyFIFO()
{
  
  std::vector<uint32_t> cSpy[2];
  const size_t N = 320;
  for (int i = 0; i < 2; ++i) {
    { //while (1) {
      std::vector<uint32_t> tmp = regManager->ReadBlockRegValue(i == 0 ? "fifo.spy_A" : "fifo.spy_B", N);
      std::vector<uint32_t>::iterator it = std::find(tmp.begin(), tmp.end(), 0);
//      int l = it - tmp.begin();
//      if (l == 0)
//	break;
      cSpy[i].insert(cSpy[i].end(), tmp.begin(), it);
    }
  }

  if (getPrintlevel()&16) {
    std::cout  << std::endl << "TBM_SPY FIFO A (size " << cSpy[0].size() << "):" << std::endl;
    prettyprintSpyFIFO(cSpy[0]);
    std::cout << std::endl << "TBM_SPY FIFO B (size " << cSpy[1].size() << "):" << std::endl;
    prettyprintSpyFIFO(cSpy[1]);
  }

//append content of Spy Fifo B to A and return
    return std::vector<uint32_t>();
//    std::vector<uint32_t> cAppendedSPyFifo = cSpyA;
//    cAppendedSPyFifo.insert(cSpyA.end(), cSpyB.begin(), cSpyB.end());
//    return cAppendedSPyFifo;
}


int PixelFEDInterfacePh1::drainSpyFifo(uint32_t* data) {
  const size_t MAX_SPY_FIFO = 1024;
  std::vector<uint32_t> v(readSpyFIFO());
  const int ie(min(v.size(), MAX_SPY_FIFO));
  for (int i = 0; i < ie; ++i)
    data[i] = v[i];
  return ie;
}

PixelFEDInterfacePh1::encfifo1 PixelFEDInterfacePh1::decodeFIFO1Stream(const std::vector<uint32_t>& fifo, const std::vector<uint32_t>& markers) {
  encfifo1 f;

  for (size_t i = 0, ie = fifo.size(); i < ie; ++i) {
    const uint32_t word = fifo[i];
    const uint32_t marker = markers[i];

    if (marker == 8) {
      f.found = true;
      f.ch = (word >> 26) & 0x3f;
      f.id_tbm_h = (word >> 21) & 0x1f;
      f.tbm_h = (word >> 9) & 0xff;
      f.event = word & 0xff;
    }
    else if (marker == 12) {
      encfifo1roc r;
      r.ch = (word >> 26) & 0x3f;
      r.roc = (word >> 21) & 0x1f;
      r.rb = word & 0xff;
      f.rocs.push_back(r);
    }
    else if (marker == 1) {
      encfifo1hit h;
      h.ch = (word >> 26) & 0x3f;
      h.roc = (word >> 21) & 0x1f;
      h.dcol = (word >> 16) & 0x1f;
      h.pxl = (word >> 8) & 0xff;
      h.ph = (word) & 0xff;
      f.hits.push_back(h);
    }
    else if (marker == 4) {
      f.ch_tbm_t = (word >> 26) & 0x3f;
      f.tbm_t1 = word & 0xff;
      f.tbm_t2 = (word >> 12) & 0xff;
      f.id_tbm_t = (word >> 21) & 0x1f;
    }
    else if (marker == 6) {
      f.ch_evt_t = (word >> 26) & 0x3f;
      assert(f.ch == f.ch_evt_t);
      f.id_evt_t = (word >> 21) & 0x1f;
      f.marker = word & 0x1fffff;
    }
  }

  return f;
}

void PixelFEDInterfacePh1::prettyprintFIFO1Stream( const std::vector<uint32_t>& pFifoVec, const std::vector<uint32_t>& pMarkerVec) {
    std::cout << "----------------------------------------------------------------------------------" << std::endl;

    for (uint32_t cIndex = 0; cIndex < pFifoVec.size(); cIndex++ )
    {
      if (pFifoVec[cIndex] != 0 || pMarkerVec[cIndex] != 0)
        std::cout << "word #" << cIndex << ": 0x" << std::hex << pFifoVec[cIndex] << "  marker: 0x" << pMarkerVec[cIndex] << std::dec << std::endl;
        if (pMarkerVec.at (cIndex) == 8)
        {
            // Event Header
            std::cout << std::dec << "    Header: "
               << "CH: " << ( (pFifoVec.at (cIndex) >> 26) & 0x3f )
               << " ID: " <<  ( (pFifoVec.at (cIndex) >> 21) & 0x1f )
               << " TBM_H: " <<  ( (pFifoVec.at (cIndex) >> 9) & 0xff )
               << " EVT Nr: " <<  ( (pFifoVec.at (cIndex) ) & 0xff ) << std::endl;
        }

        if (pMarkerVec.at (cIndex) == 12)
            std::cout << std::dec << "ROC Header: "
               << "CH: " << ( (pFifoVec.at (cIndex) >> 26) & 0x3f  )
               << " ROC Nr: " <<  ( (pFifoVec.at (cIndex) >> 21) & 0x1f )
               << " Status: " << (  (pFifoVec.at (cIndex) ) & 0xff ) << std::endl;

        if (pMarkerVec.at (cIndex) == 1)
            std::cout  << std::dec << "            "
                << "CH: " << ( (pFifoVec.at (cIndex) >> 26) & 0x3f )
                << " ROC Nr: " <<  ( (pFifoVec.at (cIndex) >> 21) & 0x1f )
                << " DC: " <<  ( (pFifoVec.at (cIndex) >> 16) & 0x1f )
                << " PXL: " <<  ( (pFifoVec.at (cIndex) >> 8) & 0xff )
                <<  " PH: " <<  ( (pFifoVec.at (cIndex) ) & 0xff ) << std::endl;

        if (pMarkerVec.at (cIndex) == 4)
            // TBM Trailer
            std::cout << std::dec << "   Trailer: "
               << "CH: " << ( (pFifoVec.at (cIndex) >> 26) & 0x3f )
               << " ID: " <<  ( (pFifoVec.at (cIndex) >> 21) & 0x1f )
               << " TBM_T2: " <<  ( (pFifoVec.at (cIndex) >> 12) & 0xff )
               << " TBM_T1: " <<  ( (pFifoVec.at (cIndex) ) & 0xff ) << std::endl;

        if (pMarkerVec.at (cIndex) == 6)
            // Event Trailer
            std::cout << std::dec << "Event Trailer: "
               << "CH: " << ( (pFifoVec.at (cIndex) >> 26) & 0x3f )
               << " ID: " <<  ( (pFifoVec.at (cIndex) >> 21) & 0x1f )
               << " marker: " <<  ( (pFifoVec.at (cIndex) ) & 0x1fffff ) << std::endl;
    }

    std::cout << "----------------------------------------------------------------------------------" << std::endl;
}

PixelFEDInterfacePh1::digfifo1 PixelFEDInterfacePh1::readFIFO1(bool print) {
  digfifo1 df;

  df.cFifo1A  = regManager->ReadBlockRegValue("fifo.spy_1_A", 2048);
  df.cMarkerA = regManager->ReadBlockRegValue("fifo.spy_1_A_marker", 2048);
  df.cFifo1B  = regManager->ReadBlockRegValue("fifo.spy_1_B", 2048);
  df.cMarkerB = regManager->ReadBlockRegValue("fifo.spy_1_B_marker", 2048);

  df.a = decodeFIFO1Stream(df.cFifo1A, df.cMarkerA);
  df.b = decodeFIFO1Stream(df.cFifo1B, df.cMarkerB);
  //  assert(df.a.event == 0 || df.b.event == 0 || df.a.event == df.b.event);
  if (print) {
    std::cout << "FIFO1 for A:\n";
    prettyprintFIFO1Stream(df.cFifo1A, df.cMarkerA);
    std::cout << "FIFO1 for B:\n";
    prettyprintFIFO1Stream(df.cFifo1B, df.cMarkerB);
  }

  return df;
}

int PixelFEDInterfacePh1::drainFifo1(uint32_t *data) {
  return 0;
}

int PixelFEDInterfacePh1::drainErrorFifo(uint32_t *data) {
  return 0;
}

int PixelFEDInterfacePh1::drainTemperatureFifo(uint32_t* data) {
  return 0;
}

int PixelFEDInterfacePh1::drainTTSFifo(uint32_t *data) {
  return 0;
}

void PixelFEDInterfacePh1::SelectDaqDDR( uint32_t pNthAcq )
{
    fStrDDR  = ( ( pNthAcq % 2 + 1 ) == 1 ? "DDR0" : "DDR1" );
    fStrDDRControl = ( ( pNthAcq % 2 + 1 ) == 1 ? "pixfed_ctrl_regs.DDR0_ctrl_sel" : "pixfed_ctrl_regs.DDR1_ctrl_sel" );
    fStrFull = ( ( pNthAcq % 2 + 1 ) == 1 ? "pixfed_stat_regs.DDR0_full" : "pixfed_stat_regs.DDR1_full" );
    fStrReadout = ( ( pNthAcq % 2 + 1 ) == 1 ? "pixfed_ctrl_regs.DDR0_end_readout" : "pixfed_ctrl_regs.DDR1_end_readout" );
}

void prettyprintTBMFIFO(const std::vector<uint32_t>& pData )
{
  std::cout << "Global TBM Readout FIFO: size " << pData.size() << std::endl;
    //now I need to do something with the Data that I read into cData
    int cIndex = 0;
    uint32_t cPreviousWord = 0;
    for ( size_t i = 0; i < pData.size(); ++i)
    {
      uint32_t cWord = pData[i];
      std::cout << std::setw(5) << i << ": " << std::hex << std::setw(8) << cWord << std::dec << std::endl;
        if (cIndex % 2 == 0)
            cPreviousWord = cWord;

        else if (cPreviousWord == 0x1)
        {
            //std::cout << cWord <<  std::endl;
            std::cout << "    Pixel Hit: CH: " << ((cWord >> 26) & 0x3f) << " ROC: " << ((cWord >> 21) & 0x1f) << " DC: " << ((cWord >> 16) & 0x1f) << " ROW: " << ((cWord >> 8) & 0xff) << " PH: " << (cWord & 0xff) << std::dec << std::endl;
        }
        //else if (cPreviousWord == 0x6)
        //{
        ////std::cout << cWord <<  std::endl;
        //std::cout << "Event Trailer: CH: " << ((cWord >> 26) & 0x3f) << " ID: " << ((cWord >> 21) & 0x1f) << " marker: " << (cWord & 0x1fffff) << " ROW: " << ((cWord >> 8) & 0xff) << " PH: " << (cWord & 0xff) << std::dec << std::endl;
        //}
        else if (cPreviousWord == 0x8)
        {
            //std::cout << cWord <<  std::endl;
            std::cout << "Event Header: CH: " << ((cWord >> 26) & 0x3f) << " ID: " << ((cWord >> 21) & 0x1f) << " TBM H: " << ((cWord >> 16) & 0x1f) << " ROW: " << ((cWord >> 9) & 0xff) << " EventNumber: " << (cWord & 0xff) << std::dec << std::endl;
        }
        else if (cPreviousWord == 0xC)
        {
            //std::cout << cWord <<  std::endl;
            std::cout << " ROC Header: CH: " << ((cWord >> 26) & 0x3f) << " ROC Nr: " << ((cWord >> 21) & 0x1f) << " Status : " << (cWord  & 0xff) << std::dec << std::endl;
        }
        else if (cPreviousWord == 0x4)
        {
            //std::cout << cWord <<  std::endl;
            std::cout << " TBM Trailer: CH: " << ((cWord >> 26) & 0x3f) << " ID: " << ((cWord >> 21) & 0x1f) << " TBM T2: " << ((cWord >> 12) & 0xff) << " TBM_T1: " << (cWord & 0xff) << std::dec << std::endl;
        }
        cIndex++;
    }
}

std::vector<uint32_t> PixelFEDInterfacePh1::ReadData(uint32_t cBlockSize)
{
    //    std::cout << "JJJ READ DATA " << cBlockSize << std::endl;
    //std::chrono::milliseconds cWait( 10 );
    // the fNthAcq variable is automatically used to determine which DDR FIFO to read - so it has to be incremented in this method!

    // first find which DDR bank to read
    SelectDaqDDR( fNthAcq );
    std::cout << "Querying " << fStrDDR << " for FULL condition!" << std::endl;

    uhal::ValWord<uint32_t> cVal;
    int tries = 0;
    int maxtries = 100;
    do
    {
        cVal = regManager->ReadReg( fStrFull );
        if ( cVal == 0 && tries++ < maxtries ) usleep(1000);
    }
    while ( cVal == 0 && tries < maxtries);
    std::cout << fStrDDR << " full: " << regManager->ReadReg( fStrFull ) << " after " << tries << " tries " << std::endl;

    // DDR control: 0 = ipbus, 1 = user
    regManager->WriteReg( fStrDDRControl, 0 );
    usleep(10000);
    std::cout << "Starting block read of " << fStrDDR << std::endl;

    std::vector<uint32_t> cData;
    for (int i = 0; i < 5; ++i) {
      std::vector<uint32_t> tmp = regManager->ReadBlockRegValue( fStrDDR, cBlockSize );
      cData.insert(cData.end(), tmp.begin(), tmp.end());
    }

    regManager->WriteReg( fStrDDRControl , 1 );
    usleep(10000);
    regManager->WriteReg( fStrReadout, 1 );
    usleep(10000);

    // full handshake between SW & FW
    while ( regManager->ReadReg( fStrFull ) == 1 )
      usleep(10000);
    regManager->WriteReg( fStrReadout, 0 );

    //prettyprintTBMFIFO(cData);
    fNthAcq++;
    return cData;
}


int PixelFEDInterfacePh1::spySlink64(uint64_t *data) {
  ++slink64calls;
  std::cout << "fed #" <<  pixelFEDCard.fedNumber << " slink64call #" << slink64calls << std::endl;
  //readSpyFIFO();
  readFIFO1(true);

  usleep(2000);

  uhal::ValWord<uint32_t> cVal = 0;
  //uint32_t mycntword = 0;
  int sleepcnt=0;
  bool our_timeout = false;
  do {
    cVal = regManager->ReadReg("pixfed_stat_regs.DDR0_full");
    if (cVal == 0) usleep(100);
    sleepcnt++;
    //if(sleepcnt > 1000) mycntword = regManager->ReadReg("pixfed_stat_regs.cnt_word32from_start");
    //if(mycntword>5){std::cout<<mycntword<<" words in the ddr"<<std::endl; usleep(300000);}
    if (sleepcnt > 20000) {
      cout << "\033[1m\033[32mSOFTWARE TIMEOUT\033[0m" << std::endl;
      our_timeout = true;
      break;
    }
  }
  while ( cVal == 0 );
  const uint32_t cNWords32 = regManager->ReadReg("pixfed_stat_regs.cnt_word32from_start");
  const uint32_t cBlockSize = cNWords32 + (2 * 2 * last_calib_mode_nevents);
  usleep(10);
  std::vector<uint32_t> cData = regManager->ReadBlockRegValue("DDR0", cBlockSize);
  usleep(10);
  regManager->WriteReg("pixfed_ctrl_regs.DDR0_end_readout", 1);
  usleep(10);
  if (our_timeout) {
    std::cout << "error decoder:\n";
    ErrorFIFODecoder ed(&cData[2], cData.size()-4);
    ed.printToStream(std::cout);
    return 0;
  }

  while (regManager->ReadReg("pixfed_stat_regs.DDR0_full") == 1)
    usleep(10);

  regManager->WriteReg("pixfed_ctrl_regs.DDR0_end_readout", 0);

  const size_t ndata = cData.size();
  assert(ndata % 2 == 0);
  size_t ndata64 = ndata / 2; // + (ndata % 2 ? 1 : 0);
  //std::vector<uint64_t> data64(ndata64, 0ULL);

  for (size_t j = 0; j < ndata64; ++j)
    data[j] = (uint64_t(cData[j*2]) << 32) | cData[j*2+1];

  const uint32_t evnum = cData[0] & 0xFFFFFF;
  const uint32_t bxnum = cData[1] >> 20;
  bxs.push_back(bxnum);
  if (bxs.size() == 1000) {
    printf("last 1000 bxnums: [");
    for (int ibx = 0; ibx < 1000; ++ibx)
      printf("%u,", bxs[ibx]);
    printf("]\n");
    bxs.clear();
  }
  std::cout << "slink64call #" << slink64calls << ", fed event #" << evnum << " ";
  if (evnum != slink64calls) std::cout << "\033[1m\033[31mDISAGREE by " << int(evnum) - int(slink64calls) << "\033[0m";
  std::cout << ", bx #" << bxnum << "; blocksize: " << cBlockSize << std::endl;

#if 1
  std::cout << "slink 32-bit words from FW:\n";
  for (size_t i = 0; i < ndata; ++i)
    std::cout << setw(3) << i << ": " << "0x" << std::hex << std::setw(8) << std::setfill('0') << cData[i] << std::dec << "\n";

  std::cout << "error decoder:\n";
  ErrorFIFODecoder ed(&cData[2], cData.size()-4);
  ed.printToStream(std::cout);

  std::cout << "packed 64 bit:\n";
  for (size_t j = 0; j < ndata64; ++j) {
    std::cout << std::setw(2) << j << " = 0x " << std::hex << std::setw(8) << std::setfill('0') << (data[j]>>32) << " " << std::setw(8) << std::setfill('0') << (data[j] & 0xFFFFFFFF) << std::dec << std::endl;
    //std::cout << setw(3) << j << ": " << "0x" << std::hex << std::setw(16) << std::setfill('0') << data[j] << std::dec << std::endl;
  }

  FIFO3Decoder decode3(data);
  //for (size_t i = 0; i <= ndata64; ++i)
  std::cout << "FIFO3Decoder thinks:\n" << "nhits: " << decode3.nhits() << std::endl;
  for (unsigned i = 0; i < decode3.nhits(); ++i) {
    //const PixelROCName& rocname = theNameTranslation_->ROCNameFromFEDChannelROC(fednumber, decode3.channel(i), decode3.rocid(i)-1);
    std::cout << "#" << i << ": ch: " << decode3.channel(i)
              << " rocid: " << decode3.rocid(i)
      //    << " (" << rocname << ")"
              << " dcol: " << decode3.dcol(i)
              << " pxl: " << decode3.pxl(i) << " pulseheight: " << decode3.pulseheight(i)
              << " col: " << decode3.column(i) << " row: " << decode3.row(i) << std::endl;
  }
#endif

#if 0
  if (f.a.hits.size() + f.b.hits.size() != decode3.nhits()) {
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "Laurent says " << decode3.nhits() << ". this doesn't agree with Helmut, who says " << f.a.hits.size() + f.b.hits.size() << "\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************\n";
    std::cout << "********************************************************************************" << std::endl;
  }

  const bool myfakefifo3 = false;
  if (myfakefifo3) {
    data[0] = 0x5000000000000000;
    data[0] |= uint64_t(f.a.event & 0xffffff) << 32;
    data[0] |= uint64_t(pixelFEDCard.fedNumber) << 8;
    size_t j = 1;
    const bool oldway = true;
    if (oldway) {
      for (size_t i = 0; i < f.a.hits.size(); ++i, ++j) {
        encfifo1hit h = f.a.hits[i];
        data[j] = (h.ch << 26) | (h.roc << 21) | (h.dcol << 16) | (h.pxl << 8) | h.ph;
        data[j] |= uint64_t(0x1b) << 53;
      }
      for (size_t i = 0; i < f.b.hits.size(); ++i, ++j) {
        encfifo1hit h = f.b.hits[i];
        data[j] = (h.ch << 26) | (h.roc << 21) | (h.dcol << 16) | (h.pxl << 8) | h.ph;
        data[j] |= uint64_t(0x1b) << 53;
      }
    }
    else {
      size_t ii = 0;
      uint32_t last_w = 0;
      for (size_t i = 0; i < f.a.hits.size(); ++i, ++ii) {
        encfifo1hit h = f.a.hits[i];
        uint32_t w = (h.ch << 26) | (h.roc << 21) | (h.dcol << 16) | (h.pxl << 8) | h.ph;
        if (ii % 2 == 1)
          data[1+ii/2] = (uint64_t(w) << 32) | last_w;
        last_w = w;
      }
      if (ii % 2 == 1)
        data[1+ii/2] = (uint64_t(0x1b) << 53) | (1ULL << 32) | last_w;

      for (size_t i = 0; i < f.b.hits.size(); ++i, ++ii) {
        encfifo1hit h = f.b.hits[i];
        uint32_t w = (h.ch << 26) | (h.roc << 21) | (h.dcol << 16) | (h.pxl << 8) | h.ph;
        if (ii % 2 == 1)
          data[1+ii/2] = (uint64_t(w) << 32) | last_w;
        last_w = w;
      }
      if (ii % 2 == 1)
        data[1+ii/2] = (uint64_t(0x1b) << 53) | (1ULL << 32) | last_w;

      j = ii/2 + 1;
    }
    data[j] = 0xa000000000000000;
    data[j] |= uint64_t((j+1)&0x3fff) << 32;
    ++j;

    ndata64 = j;

    // maybe we trashed the 5 in the msb, it's all the decoder cares about...
    data[0] = 0x5000000000000000 | (data[0] & 0xFFFFFFFFFFFFFFF);

    std::cout << "my fake fifo3:\n";
    for (size_t i = 0; i < j; ++i)
      std::cout << std::hex << "0x" << std::setw(16) << std::setfill('0') << data[i] << std::endl;
  }
#endif

  //std::vector<uint32_t> cData = ReadData(1024);
  //cData = ReadData(1024);
  return int(ndata64);
  //return int(j);
}

bool PixelFEDInterfacePh1::isWholeEvent(uint32_t nTries) {
  return false;
}

bool PixelFEDInterfacePh1::isNewEvent(uint32_t nTries) {
  return false;
}

int PixelFEDInterfacePh1::enableSpyMemory(const int enable) {
  // card.modeRegister ?
  return setModeRegister(pixelFEDCard.modeRegister);
}

void PixelFEDInterfacePh1::printBoardInfo() {
}

int PixelFEDInterfacePh1::loadFedIDRegister() {
  if(getPrintlevel()&1)cout<<"Load FEDID register from DB 0x"<<hex<<pixelFEDCard.fedNumber<<dec<<endl;
  return setFedIDRegister(pixelFEDCard.fedNumber);
}

int PixelFEDInterfacePh1::setFedIDRegister(const int value) {
  cout<<"Set FEDID register "<<hex<<value<<dec<<endl;
  // write here
  int got = getFedIDRegister();
  if (value != got) cout<<"soft FEDID = "<<value<<" doesn't match hard board FEDID = "<<got<<endl;
  return value == got;
}

int PixelFEDInterfacePh1::getFedIDRegister() {
  return 0;
}

int PixelFEDInterfacePh1::loadControlRegister() {
  if(getPrintlevel()&1)cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" Load Control register from DB 0x"<<hex<<pixelFEDCard.Ccntrl<<dec<<endl;
  return setControlRegister(pixelFEDCard.Ccntrl);
}

int PixelFEDInterfacePh1::setControlRegister(const int value) {
  if(getPrintlevel()&1)cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" Set Control register "<<hex<<value<<dec<<endl;
  // write here
  pixelFEDCard.Ccntrl=value; // stored this value   
  return false;
}

int PixelFEDInterfacePh1::getControlRegister() {
  return 0;
}

int PixelFEDInterfacePh1::loadModeRegister() {
  if(getPrintlevel()&1)cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" Load Mode register from DB 0x"<<hex<<pixelFEDCard.Ccntrl<<dec<<endl;
  return setModeRegister(pixelFEDCard.modeRegister);
}

int PixelFEDInterfacePh1::setModeRegister(int value) {
  if(getPrintlevel()&1)cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" Set Mode register "<<hex<<value<<dec<<endl;
  // write here
  pixelFEDCard.modeRegister=value; // stored this value   
  return false;
}

int PixelFEDInterfacePh1::getModeRegister() {
  return 0;
}

void PixelFEDInterfacePh1::set_PrivateWord(uint32_t pword) {
}

void PixelFEDInterfacePh1::resetSlink() {
}

bool PixelFEDInterfacePh1::checkFEDChannelSEU() {
  /*
    Check to see if the channels that are currently on match what we expect. If not
    increment the counter and return true. Note that this assumes that the method won't
    be called again until the SEU is fixed. Otherwise, the counter will be incremented multiple
    times for the same SEU.
  */
  bool foundSEU = false;
  uint64_t enbable_current_i = 0;
  // read here
  enbable_t enbable_current(enbable_current_i);

  // Note: since N_enbable_expected is bitset<9>, this only compares the first 9 bits
  if (enbable_current != enbable_expected && enbable_current != enbable_last) {
    foundSEU = true;
    cout << "Detected FEDChannel SEU in FED " << pixelFEDCard.fedNumber << endl;
    cout << "Expected " << enbable_expected << " Found " << enbable_current << " Last " << enbable_last << endl;
    incrementSEUCountersFromEnbableBits(num_SEU, enbable_current, enbable_last);
  }

  enbable_last = enbable_current;

  return foundSEU;
}

void PixelFEDInterfacePh1::incrementSEUCountersFromEnbableBits(vector<int> &counter, enbable_t current, enbable_t last) {
  for(size_t i = 0; i < current.size(); i++)
    if (current[i] != last[i])
      counter[i]++;
}

bool PixelFEDInterfacePh1::checkSEUCounters(int threshold) {
  /*
    Check to see if any of the channels have more than threshold SEUs.
    If so, return true and set expected enbable bit for that channel
    to off.
    Otherwise, return false
  */
  bool return_val = false;
  cout << "Checking for more than " << threshold << " SEUs in FED " << pixelFEDCard.fedNumber << endl;
  cout << "Channels with too many SEUs: ";
  for (size_t i=0; i<48; i++)
    {
      if (num_SEU[i] >= threshold) {
        enbable_expected[i] = 1; // 1 is off
        cout << " " << 1+i << "(" << num_SEU[i] << ")";
        return_val = true;
      }
    }
  if (return_val) {
    cout << ". Disabling." << endl;
    cout << "Setting runDegraded flag for FED " << pixelFEDCard.fedNumber << endl;
    runDegraded_ = true;
  } else cout << endl;
  return return_val;
}

void PixelFEDInterfacePh1::resetEnbableBits() {
  // Get the current values of higher bits in these registers, so we can leave them alone

  //uint64_t OtherConfigBits = 0;
  //uint64_t enbable_exp = 0; //enbable_expected.to_ullong(); 
  //for (int i = 0; i < 48; ++i)
  //  if (enbable_expected[i])
  //    enbable_exp |= 1<<i;
  //uint64_t write = OtherConfigBits | enbable_exp;
  //  write;
}

void PixelFEDInterfacePh1::storeEnbableBits() {
  enbable_expected = pixelFEDCard.cntrl_utca;
  enbable_last = enbable_expected;
}

void PixelFEDInterfacePh1::resetSEUCountAndDegradeState(void) {
  cout << "reset SEU counters and the runDegrade flag " << endl;
  // reset the state back to running 
  runDegraded_ = false;
  // clear the count flag
  num_SEU.assign(48, 0);
  // reset the expected state to default
  storeEnbableBits();
}

void PixelFEDInterfacePh1::testTTSbits(uint32_t data, int enable) {
  //will turn on the test bits indicate in bits 0...3 as long as bit 31 is set
  //As of this writing, the bits indicated are: 0(Warn), 1(OOS), 2(Busy), 4(Ready)
  //Use a 1 or any >1 to enable, a 0 or <0 to disable
  if (enable>0)
    data = (data | 0x80000000) & 0x8000000f;
  else
    data = data & 0xf;
  // write here
}

void PixelFEDInterfacePh1::setXY(int X, int Y) {
}

int PixelFEDInterfacePh1::getXYCount() {
  return 0;
}

void PixelFEDInterfacePh1::resetXYCount() {
}

uint32_t PixelFEDInterfacePh1::getNumFakeEvents() {
  return 0;
}

void PixelFEDInterfacePh1::resetNumFakeEvents() {
}

int PixelFEDInterfacePh1::readEventCounter() {
  return 0;
}

uint32_t PixelFEDInterfacePh1::getFifoStatus() {
  return 0;
}

uint32_t PixelFEDInterfacePh1::linkFullFlag() {
  return 0;
}

uint32_t PixelFEDInterfacePh1::numPLLLocks() {
  return 0;
}

uint32_t PixelFEDInterfacePh1::getFifoFillLevel() {
  return 0;
}

uint64_t PixelFEDInterfacePh1::getSkippedChannels() {
  return 0;
}

uint32_t PixelFEDInterfacePh1::getErrorReport(int ch) {
  return 0;
}

uint32_t PixelFEDInterfacePh1::getTimeoutReport(int ch) {
  return 0;
}

int PixelFEDInterfacePh1::TTCRX_I2C_REG_READ(int Register_Nr) {
  return 0;
}

int PixelFEDInterfacePh1::TTCRX_I2C_REG_WRITE(int Register_Nr, int Value) {
  return 0;
}
