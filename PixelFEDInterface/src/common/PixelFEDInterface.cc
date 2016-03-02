#include <iostream>
#include <time.h>
#include <assert.h>
#include <unistd.h>

#include "PixelFEDInterface/include/PixelFEDInterface.h"
#include "PixelUtilities/PixelTestStandUtilities/include/PixelTimer.h"

using namespace std;

PixelFEDInterface::PixelFEDInterface(Ph2_HwInterface::RegManager * const rm)
  : Printlevel(1),
    regManager(rm)
{
  num_SEU.assign(48, 0);
}

PixelFEDInterface::~PixelFEDInterface() {
}

int PixelFEDInterface::setup(const string& fileName) {
  pixelFEDCard = pos::PixelPh1FEDCard(fileName);
  return setup();
}

int PixelFEDInterface::setup(pos::PixelPh1FEDCard& pfc) {
  pixelFEDCard = pfc;
  return setup();
}

int PixelFEDInterface::setup() {
  fRegMapFilename[FMC0_Fitel0] = fitel_fn_base + "/FMC0_Fitel0.txt";
  fRegMapFilename[FMC0_Fitel1] = fitel_fn_base + "/FMC0_Fitel1.txt";
  fRegMapFilename[FMC1_Fitel0] = fitel_fn_base + "/FMC1_Fitel0.txt";
  fRegMapFilename[FMC1_Fitel1] = fitel_fn_base + "/FMC1_Fitel1.txt";

  LoadFitelRegMap(0, 0);
  LoadFitelRegMap(0, 1);
  LoadFitelRegMap(1, 0);
  LoadFitelRegMap(1, 1);


  std::vector<std::pair<std::string, uint32_t> > cVecReg;

  //Primary Configuration
  cVecReg.push_back( {"pixfed_ctrl_regs.PC_CONFIG_OK", 0} );
  //cVecReg.push_back( {"pixfed_ctrl_regs.INT_TRIGGER_EN", 0} );
  cVecReg.push_back( {"pixfed_ctrl_regs.rx_index_sel_en", 0} );

  cVecReg.push_back( {"pixfed_ctrl_regs.DDR0_end_readout", 0} );
  cVecReg.push_back( {"pixfed_ctrl_regs.DDR1_end_readout", 0} );

  //cVecReg.push_back( {"pixfed_ctrl_regs.CMD_START_BY_PC", 0} );

  // fitel I2C bus reset & fifo TX & RX reset
  cVecReg.push_back({"pixfed_ctrl_regs.fitel_i2c_cmd_reset", 1});

  // the FW needs to be aware of the true 32 bit workd Block size for some reason! This is the Packet_nb_true in the python script?!
  //computeBlockSize( pFakeData );
  const uint32_t fBlockSize32 = 0x7f;
  cVecReg.push_back( {"pixfed_ctrl_regs.PACKET_NB", fBlockSize32 } );

  // <!--Used to set the CLK input to the TTC clock from the BP - 3 is XTAL, 0 is BP-->
  cVecReg.push_back( {"ctrl.ttc_xpoint_A_out3", 0x0} );

  cVecReg.push_back( {"pixfed_ctrl_regs.TBM_MASK_1", 0xffffffff} );
  cVecReg.push_back( {"pixfed_ctrl_regs.TBM_MASK_2", 0xffffc30f} );
  cVecReg.push_back( {"pixfed_ctrl_regs.TBM_MASK_3", 0xffffffff} );
  cVecReg.push_back( {"pixfed_ctrl_regs.TRIGGER_SEL", 0x0} );
  cVecReg.push_back( {"pixfed_ctrl_regs.data_type", 0x0} );

  regManager->WriteStackReg(cVecReg);
  cVecReg.clear();

  cVecReg.push_back({"pixfed_ctrl_regs.fitel_i2c_cmd_reset", 0});
  cVecReg.push_back({"pixfed_ctrl_regs.fitel_config_req", 0});
  cVecReg.push_back( {"pixfed_ctrl_regs.PC_CONFIG_OK", 1} );

  regManager->WriteStackReg(cVecReg);
  cVecReg.clear();

  usleep(200000);

  int cDDR3calibrated = regManager->ReadReg("pixfed_stat_regs.ddr3_init_calib_done") & 1;

  ConfigureFitel(0, 0, true);
  ConfigureFitel(0, 1, true);

  fNthAcq = 0;

  getBoardInfo();

  return cDDR3calibrated;
}

void PixelFEDInterface::loadFPGA() {
}


std::string PixelFEDInterface::getBoardType()
{
    // adapt me!
    std::string cBoardTypeString;

    uhal::ValWord<uint32_t> cBoardType = regManager->ReadReg( "board_id" );

    char cChar = ( ( cBoardType & 0xFF000000 ) >> 24 );
    cBoardTypeString.push_back( cChar );

    cChar = ( ( cBoardType & 0x00FF0000 ) >> 16 );
    cBoardTypeString.push_back( cChar );

    cChar = ( ( cBoardType & 0x0000FF00 ) >> 8 );
    cBoardTypeString.push_back( cChar );

    cChar = ( cBoardType & 0x000000FF );
    cBoardTypeString.push_back( cChar );

    return cBoardTypeString;

}

void PixelFEDInterface::getFEDNetworkParameters()
{
    std::cout << "MAC & IP Source: " << regManager->ReadReg("mac_ip_source") << std::endl;

    std::cout << "MAC Address: " << std::hex << regManager->ReadReg("mac_b5") << ":" << regManager->ReadReg("mac_b4") << ":" << regManager->ReadReg("mac_b3") << ":" << regManager->ReadReg("mac_b2") << ":" << regManager->ReadReg("mac_b1") << ":" << regManager->ReadReg("mac_b0") << std::dec << std::endl;
}

void PixelFEDInterface::getBoardInfo()
{
    std::cout << std::endl << "Board Type: " << getBoardType() << std::endl;
    getFEDNetworkParameters();
    std::string cBoardTypeString;

    uhal::ValWord<uint32_t> cBoardType = regManager->ReadReg( "pixfed_stat_regs.user_ascii_code_01to04" );

    char cChar = ( ( cBoardType & 0xFF000000 ) >> 24 );
    cBoardTypeString.push_back( cChar );

    cChar = ( ( cBoardType & 0x00FF0000 ) >> 16 );
    cBoardTypeString.push_back( cChar );

    cChar = ( ( cBoardType & 0x0000FF00 ) >> 8 );
    cBoardTypeString.push_back( cChar );

    cChar = ( cBoardType & 0x000000FF );
    cBoardTypeString.push_back( cChar );

    cBoardType = regManager->ReadReg( "pixfed_stat_regs.user_ascii_code_05to08" );
    cChar = ( ( cBoardType & 0xFF000000 ) >> 24 );
    cBoardTypeString.push_back( cChar );

    cChar = ( ( cBoardType & 0x00FF0000 ) >> 16 );
    cBoardTypeString.push_back( cChar );

    cChar = ( ( cBoardType & 0x0000FF00 ) >> 8 );
    cBoardTypeString.push_back( cChar );

    cChar = ( ( cBoardType & 0x00000000 ) );
    cBoardTypeString.push_back( cChar );

    std::cout << "Board Use: " << cBoardTypeString << std::endl;

    std::cout << "FW version IPHC : " << regManager->ReadReg( "pixfed_stat_regs.user_iphc_fw_id.fw_ver_nb" ) << "." << regManager->ReadReg( "pixfed_stat_regs.user_iphc_fw_id.archi_ver_nb" ) << "; Date: " << regManager->ReadReg( "pixfed_stat_regs.user_iphc_fw_id.fw_ver_day" ) << "." << regManager->ReadReg( "pixfed_stat_regs.user_iphc_fw_id.fw_ver_month" ) << "." << regManager->ReadReg( "pixfed_stat_regs.user_iphc_fw_id.fw_ver_year" ) <<  std::endl;
    std::cout << "FW version HEPHY : " << regManager->ReadReg( "pixfed_stat_regs.user_hephy_fw_id.fw_ver_nb" ) << "." << regManager->ReadReg( "pixfed_stat_regs.user_hephy_fw_id.archi_ver_nb" ) << "; Date: " << regManager->ReadReg( "pixfed_stat_regs.user_hephy_fw_id.fw_ver_day" ) << "." << regManager->ReadReg( "pixfed_stat_regs.user_hephy_fw_id.fw_ver_month" ) << "." << regManager->ReadReg( "pixfed_stat_regs.user_hephy_fw_id.fw_ver_year" ) << std::endl;

    std::cout << "FMC 8 Present : " << regManager->ReadReg( "status.fmc_l8_present" ) << std::endl;
    std::cout << "FMC 12 Present : " << regManager->ReadReg( "status.fmc_l12_present" ) << std::endl << std::endl;
}


void PixelFEDInterface::disableFMCs()
{
    std::vector< std::pair<std::string, uint32_t> > cVecReg;
    cVecReg.push_back( { "fmc_pg_c2m", 0 } );
    cVecReg.push_back( { "fmc_l12_pwr_en", 0 } );
    cVecReg.push_back( { "fmc_l8_pwr_en", 0 } );
    regManager->WriteStackReg(cVecReg);
}

void PixelFEDInterface::enableFMCs()
{
    std::vector< std::pair<std::string, uint32_t> > cVecReg;
    cVecReg.push_back( { "fmc_pg_c2m", 1 } );
    cVecReg.push_back( { "fmc_l12_pwr_en", 1 } );
    cVecReg.push_back( { "fmc_l8_pwr_en", 1 } );
    regManager->WriteStackReg(cVecReg);
}

/*
void PixelFEDInterface::FlashProm( const std::string & strConfig, const char* pstrFile )
{
    checkIfUploading();

    fpgaConfig->runUpload( strConfig, pstrFile );
}

void PixelFEDInterface::JumpToFpgaConfig( const std::string & strConfig )
{
    checkIfUploading();

    fpgaConfig->jumpToImage( strConfig );
}

std::vector<std::string> PixelFEDInterface::getFpgaConfigList()
{
    checkIfUploading();
    return fpgaConfig->getFirmwareImageNames( );
}

void PixelFEDInterface::DeleteFpgaConfig( const std::string & strId )
{
    checkIfUploading();
    fpgaConfig->deleteFirmwareImage( strId );
}

void PixelFEDInterface::DownloadFpgaConfig( const std::string& strConfig, const std::string& strDest)
{
    checkIfUploading();
    fpgaConfig->runDownload( strConfig, strDest.c_str());
}

void PixelFEDInterface::checkIfUploading()
{
    if ( fpgaConfig && fpgaConfig->getUploadingFpga() > 0 )
        throw Exception( "This board is uploading an FPGA configuration" );

    if ( !fpgaConfig )
        fpgaConfig = new CtaFpgaConfig( this );
}
*/

void PixelFEDInterface::LoadFitelRegMap( int cFMCId, int cFitelId )
{

  const std::string& filename = fRegMapFilename[FitelMapNum(cFMCId, cFitelId)];

    std::ifstream file( filename.c_str(), std::ios::in );

    if ( file )
    {
        std::string line, fName, fAddress_str, fDefValue_str, fValue_str, fPermission_str;
        FitelRegItem fRegItem;

        while ( getline( file, line ) )
        {
            if ( line.find_first_not_of( " \t" ) == std::string::npos ) continue;
            if ( line.at( 0 ) == '#' || line.at( 0 ) == '*' ) continue;
            std::istringstream input( line );
            input >> fName >> fAddress_str >> fDefValue_str >> fValue_str >> fPermission_str;

            fRegItem.fAddress = strtoul(fAddress_str.c_str(), 0, 16);
            fRegItem.fDefValue = strtoul( fDefValue_str.c_str(), 0, 16 );
            fRegItem.fValue = strtoul( fValue_str.c_str(), 0, 16 );
            fRegItem.fPermission = fPermission_str.c_str()[0];

            //std::cout << fName << " "<< +fRegItem.fAddress << " " << +fRegItem.fDefValue << " " << +fRegItem.fValue << std::endl;
            fRegMap[FitelMapNum(cFMCId, cFitelId)][fName] = fRegItem;
        }

        file.close();
    }
    else
        std::cerr << "The Fitel Settings File " << filename << " could not be opened!" << std::endl;
}

void PixelFEDInterface::EncodeFitelReg( const FitelRegItem& pRegItem, uint8_t pFMCId, uint8_t pFitelId , std::vector<uint32_t>& pVecReq ) {
  pVecReq.push_back(  pFMCId  << 24 |  pFitelId << 20 |  pRegItem.fAddress << 8 | pRegItem.fValue );
}

void PixelFEDInterface::DecodeFitelReg( FitelRegItem& pRegItem, uint8_t pFMCId, uint8_t pFitelId, uint32_t pWord ) {
  //uint8_t cFMCId = ( pWord & 0xff000000 ) >> 24;
  //cFitelId = (  pWord & 0x00f00000   ) >> 20;
  pRegItem.fAddress = ( pWord & 0x0000ff00 ) >> 8;
  pRegItem.fValue = pWord & 0x000000ff;
}

void PixelFEDInterface::i2cRelease(uint32_t pTries)
{
    uint32_t cCounter = 0;
    // release
    regManager->WriteReg("pixfed_ctrl_regs.fitel_config_req", 0);
    while (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") != 0)
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

bool PixelFEDInterface::polli2cAcknowledge(uint32_t pTries)
{
    bool cSuccess = false;
    uint32_t cCounter = 0;
    // wait for command acknowledge
    while (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 0)
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
    if (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 1)
    {
        cSuccess = true;
    }
    else if (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 3)
    {
        cSuccess = false;
    }
    return cSuccess;
}

bool PixelFEDInterface::WriteFitelBlockReg(std::vector<uint32_t>& pVecReq) {
  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_addr", 0x4d);
  bool cSuccess = false;
  // write the encoded registers in the tx fifo
  regManager->WriteBlockReg("fitel_config_fifo_tx", pVecReq);
  // sent an I2C write request
  regManager->WriteReg("pixfed_ctrl_regs.fitel_config_req", 1);

  // wait for command acknowledge
  while (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 0) usleep(100);

  uint32_t cVal = regManager->ReadReg("pixfed_stat_regs.fitel_config_ack");
  //if (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 1)
  if (cVal == 1)
    {
      cSuccess = true;
    }
  //else if (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 3)
  else if (cVal == 3)
    {
      std::cout << "Error writing Registers!" << std::endl;
      cSuccess = false;
    }

  // release
  i2cRelease(10);
  return cSuccess;
}

bool PixelFEDInterface::ReadFitelBlockReg(std::vector<uint32_t>& pVecReq)
{
  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_addr", 0x4d);
  bool cSuccess = false;
  //uint32_t cVecSize = pVecReq.size();

  // write the encoded registers in the tx fifo
  regManager->WriteBlockReg("fitel_config_fifo_tx", pVecReq);
  // sent an I2C write request
  regManager->WriteReg("pixfed_ctrl_regs.fitel_config_req", 3);

  // wait for command acknowledge
  while (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 0) usleep(100);

  uint32_t cVal = regManager->ReadReg("pixfed_stat_regs.fitel_config_ack");
  //if (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 1)
  if (cVal == 1)
    {
      cSuccess = true;
    }
  //else if (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 3)
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

void PixelFEDInterface::ConfigureFitel(int cFMCId, int cFitelId , bool pVerifLoop )
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
                      //                        std::cout << "Managed to write all registers correctly in " << cIterationCounter << " Iteration(s)!" << RESET << std::endl;
                        cAllgood = true;
                    }
                }
            }
        }
    }
}

std::pair<bool, std::vector<double> > PixelFEDInterface::ReadADC( const uint8_t pFMCId, const uint8_t pFitelId) {
  // the Fitel FMC needs to be set up to be able to read the RSSI on a given Channel:
  // I2C register 0x1: set to 0x4 for RSSI, set to 0x5 for Die Temperature of the Fitel
  // Channel Control Registers: set to 0x02 to disable RSSI for this channel, set to 0x0c to enable RSSI for this channel
  // the ADC always reads the sum of all the enabled channels!
  //initial FW setup
  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_cmd_reset", 1);

  std::vector<std::pair<std::string, uint32_t> > cVecReg;
  cVecReg.push_back({"pixfed_ctrl_regs.fitel_i2c_cmd_reset", 0});
  cVecReg.push_back({"pixfed_ctrl_regs.fitel_config_req", 0});
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
  regManager->WriteReg("pixfed_ctrl_regs.fitel_config_req", 1);

  // wait for command acknowledge
  while (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 0) usleep(100);

  uint32_t cVal = regManager->ReadReg("pixfed_stat_regs.fitel_config_ack");
  bool success = cVal != 3;

  // release
  i2cRelease(10);

  //now prepare the read-back of the values
  uint8_t cNWord = 10;
  for (uint8_t cIndex = 0; cIndex < cNWord; cIndex++)
    {
      cVecRead.push_back( pFMCId << 24 | pFitelId << 20 | (0x6 + cIndex ) << 8 | 0 );
    }
  regManager->WriteReg("pixfed_ctrl_regs.fitel_i2c_addr", 0x4c);

  regManager->WriteBlockReg( "fitel_config_fifo_tx", cVecRead );
  // sent an I2C write request
  regManager->WriteReg("pixfed_ctrl_regs.fitel_config_req", 3);

  // wait for command acknowledge
  while (regManager->ReadReg("pixfed_stat_regs.fitel_config_ack") == 0) usleep(100);

  cVal = regManager->ReadReg("pixfed_stat_regs.fitel_config_ack");
  success = success && cVal != 3;

  // release
  i2cRelease(10);

  // clear the vector & read the data from the fifo
  cVecRead = regManager->ReadBlockRegValue("fitel_config_fifo_rx", cVecRead.size());

  // now convert to Voltages!
  std::vector<double> cLTCValues(cNWord / 2, 0);

  double cConstant = 0.00030518;
  // each value is hidden in 2 I2C words
  for (int cMeasurement = 0; cMeasurement < cNWord / 2; cMeasurement++)
    {
      // build the values
      uint16_t cValue = ((cVecRead.at(2 * cMeasurement) & 0x7F) << 8) + (cVecRead.at(2 * cMeasurement + 1) & 0xFF);
      uint8_t cSign = (cValue >> 14) & 0x1;

      //now the conversions are different for each of the voltages, so check by cMeasurement
      if (cMeasurement == 4)
        cLTCValues.at(cMeasurement) = (cSign == 0b1) ? (-( 32768 - cValue ) * cConstant + 2.5) : (cValue * cConstant + 2.5);

      else
        cLTCValues.at(cMeasurement) = (cSign == 0b1) ? (-( 32768 - cValue ) * cConstant) : (cValue * cConstant);
    }

  // now I have all 4 voltage values in a vector of size 5
  // V1 = cLTCValues[0]
  // V2 = cLTCValues[1]
  // V3 = cLTCValues[2]
  // V4 = cLTCValues[3]
  // Vcc = cLTCValues[4]
  //
  // the RSSI value = fabs(V3-V4) / R=150 Ohm [in Amps]
  //double cADCVal = fabs(cLTCValues.at(2) - cLTCValues.at(3)) / 150.0;
  return std::make_pair(success, cLTCValues);
}

void PixelFEDInterface::reset() {
  // JMTBAD from fedcard

  //  setup(); // JMTBAD
}

void PixelFEDInterface::resetFED() {
}

void PixelFEDInterface::armOSDFifo(int channel, int rochi, int roclo) {
}

uint32_t PixelFEDInterface::readOSDFifo(int channel) {
  return 0;
}

void PixelFEDInterface::readPhases(bool verbose, bool override_timeout) {
}

std::vector<uint32_t> PixelFEDInterface::readTransparentFIFO()
{
    //regManager->WriteReg("fe_ctrl_regs.decode_reg_reset", 1);
    //std::vector<uint32_t> cFifoVec = ReadBlockRegValue( "fifo.bit_stream", 32 );
  //    std::cout << std::endl << BOLDBLUE <<  "Transparent FIFO: " << RESET << std::endl;

    //for (auto& cWord : cFifoVec)
    //std::cout << GREEN << std::bitset<30>(cWord) << RESET << std::endl;

    std::vector<uint32_t> cFifoVec;
    //std::cout << "DEBUG: Helmut's way:" << std::endl;
    for (int i = 0; i < 32; i++)
    {
        uint32_t cWord = regManager->ReadReg("fifo.bit_stream");
        cFifoVec.push_back(cWord);
//        std::cout << GREEN << std::bitset<30>(cWord) << RESET << std::endl;
//        for (int iBit = 29; iBit >= 0; iBit--)
//        {
//            if (std::bitset<30>(cWord)[iBit] == 0) std::cout << GREEN << "_";
//            else std::cout << "-";
//        }
    }
    //    std::cout << RESET << std::endl;
    return cFifoVec;
}

int PixelFEDInterface::drainTransparentFifo(uint32_t* data) {
  const size_t MAX_TRANSPARENT_FIFO = 1024;
  std::vector<uint32_t> v(readTransparentFIFO());
  const int ie(min(v.size(), MAX_TRANSPARENT_FIFO));
  for (int i = 0; i < ie; ++i)
    data[i] = v[i];
  return ie;
}

std::vector<uint32_t> PixelFEDInterface::readSpyFIFO()
{
    std::vector<uint32_t> cSpyA;
    std::vector<uint32_t> cSpyB;

    // cSpyA = ReadBlockRegValue( "fifo.spy_A", fBlockSize / 2 );
    // cSpyB = ReadBlockRegValue( "fifo.spy_B", fBlockSize / 2 );

    cSpyA = regManager->ReadBlockRegValue( "fifo.spy_A", 4096 );
    cSpyB = regManager->ReadBlockRegValue( "fifo.spy_B", 4096 );

//    std::cout  << std::endl << BOLDBLUE << "TBM_SPY FIFO A: " << RESET << std::endl;
//    prettyprintSpyFIFO(cSpyA);
//    std::cout << std::endl << BOLDBLUE << "TBM_SPY FIFO B: " << RESET << std::endl;
//    prettyprintSpyFIFO(cSpyB);
//append content of Spy Fifo B to A and return
    std::vector<uint32_t> cAppendedSPyFifo = cSpyA;
    cAppendedSPyFifo.insert(cSpyA.end(), cSpyB.begin(), cSpyB.end());
    return cAppendedSPyFifo;
}

int PixelFEDInterface::drainSpyFifo(uint32_t* data) {
  const size_t MAX_SPY_FIFO = 1024;
  std::vector<uint32_t> v(readSpyFIFO());
  const int ie(min(v.size(), MAX_SPY_FIFO));
  for (int i = 0; i < ie; ++i)
    data[i] = v[i];
  return ie;
}

//void PixelFEDInterface::readFIFO1()
//{
//  //std::stringstream cFIFO1Str;
//    std::vector<uint32_t> cFifo1A;
//    std::vector<uint32_t> cFifo1B;
//    std::vector<uint32_t> cMarkerA;
//    std::vector<uint32_t> cMarkerB;
//
//    cFifo1A = ReadBlockRegValue("fifo.spy_1_A", fBlockSize / 4);
//    cMarkerA = ReadBlockRegValue("fifo.spy_1_A_marker", fBlockSize / 4);
//    cFifo1B = ReadBlockRegValue("fifo.spy_1_B", fBlockSize / 4);
//    cMarkerB = ReadBlockRegValue("fifo.spy_1_B_marker", fBlockSize / 4);
//    // pass cFIFO1Str as ostream to prettyPrint for later FileIo
//    //std::cout << std::endl << BOLDBLUE <<  "FIFO 1 Channel A: " << RESET << std::endl;
//    //cFIFO1Str << "FIFO 1 Channel A: " << std::endl;
//    //prettyprintFIFO1(cFifo1A, cMarkerA);
//    //prettyprintFIFO1(cFifo1A, cMarkerA, cFIFO1Str);
//    //
//    //std::cout << std::endl << BOLDBLUE << "FIFO 1 Channel B: " << RESET << std::endl;
//    //cFIFO1Str << "FIFO 1 Channel B: " << std::endl;
//    //prettyprintFIFO1(cFifo1B, cMarkerB);
//    //prettyprintFIFO1(cFifo1B, cMarkerB, cFIFO1Str);
//
//    return cFIFO1Str.str();
//}

int PixelFEDInterface::drainFifo1(uint32_t *data) {
  return 0;
}

int PixelFEDInterface::drainErrorFifo(uint32_t *data) {
  return 0;
}

int PixelFEDInterface::drainTemperatureFifo(uint32_t* data) {
  return 0;
}

int PixelFEDInterface::drainTTSFifo(uint32_t *data) {
  return 0;
}

void PixelFEDInterface::SelectDaqDDR( uint32_t pNthAcq )
{
    fStrDDR  = ( ( pNthAcq % 2 + 1 ) == 1 ? "DDR0" : "DDR1" );
    fStrDDRControl = ( ( pNthAcq % 2 + 1 ) == 1 ? "pixfed_ctrl_regs.DDR0_ctrl_sel" : "pixfed_ctrl_regs.DDR1_ctrl_sel" );
    fStrFull = ( ( pNthAcq % 2 + 1 ) == 1 ? "pixfed_stat_regs.DDR0_full" : "pixfed_stat_regs.DDR1_full" );
    fStrReadout = ( ( pNthAcq % 2 + 1 ) == 1 ? "pixfed_ctrl_regs.DDR0_end_readout" : "pixfed_ctrl_regs.DDR1_end_readout" );
}

void prettyprintTBMFIFO(const std::vector<uint32_t>& pData )
{
    std::cout << "Global TBM Readout FIFO: " << std::endl;
    //now I need to do something with the Data that I read into cData
    int cIndex = 0;
    uint32_t cPreviousWord;
    for ( size_t i = 0; i < pData.size(); ++i)
    {
      uint32_t cWord = pData[i];
        //      std::cout << std::hex << std::setw(8) << std::setfill('0');
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

std::vector<uint32_t> PixelFEDInterface::ReadData(uint32_t pBlockSize )
{
    uint32_t cBlockSize = 0;
    if (pBlockSize == 0) cBlockSize = fBlockSize;
    else cBlockSize = pBlockSize;
    std::chrono::milliseconds cWait( 10 );
    // the fNthAcq variable is automatically used to determine which DDR FIFO to read - so it has to be incremented in this method!

    // first find which DDR bank to read
    SelectDaqDDR( fNthAcq );
    //std::cout << "Querying " << fStrDDR << " for FULL condition!" << std::endl;

    uhal::ValWord<uint32_t> cVal;
    do
    {
        cVal = regManager->ReadReg( fStrFull );
        if ( cVal == 0 ) usleep(10000);
    }
    while ( cVal == 0 );
    //std::cout << fStrDDR << " full: " << regManager->ReadReg( fStrFull ) << std::endl;

    // DDR control: 0 = ipbus, 1 = user
    regManager->WriteReg( fStrDDRControl, 0 );
    usleep(10000);
    //std::cout << "Starting block read of " << fStrDDR << std::endl;

    std::vector<uint32_t> cData = regManager->ReadBlockRegValue( fStrDDR, cBlockSize );
    regManager->WriteReg( fStrDDRControl , 1 );
    usleep(10000);
    regManager->WriteReg( fStrReadout, 1 );
    usleep(10000);

    // full handshake between SW & FW
    while ( regManager->ReadReg( fStrFull ) == 1 )
      usleep(10000);
    regManager->WriteReg( fStrReadout, 0 );

    prettyprintTBMFIFO(cData);
    fNthAcq++;
    return cData;
}


int PixelFEDInterface::spySlink64(uint64_t *data) {
  ReadData(4);
  return 0;
}

bool PixelFEDInterface::isWholeEvent(uint32_t nTries) {
  return false;
}

bool PixelFEDInterface::isNewEvent(uint32_t nTries) {
  return false;
}

int PixelFEDInterface::enableSpyMemory(const int enable) {
  // pixelFEDCard.modeRegister ?
  return setModeRegister(pixelFEDCard.modeRegister);
}

uint32_t PixelFEDInterface::get_VMEFirmwareDate() {
  uint32_t iwrdat=0;
  // read here
  cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" VME FPGA (update via jtag pins only) firmware date d/m/y "
      <<dec<<((iwrdat&0xff000000)>>24)<<"/"
      <<dec<<((iwrdat&0xff0000)>>16)<<"/"
      <<dec<<((((iwrdat&0xff00)>>8)*100)+(iwrdat&0xff))<<endl;
  return iwrdat;
}

uint32_t PixelFEDInterface::get_FirmwareDate(int chip) {
  uint32_t iwrdat=0;
  if(chip != 1) return 0;
  //read here
  cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" FPGA firmware date d/m/y "
      <<dec<<((iwrdat&0xff000000)>>24)<<"/"
      <<dec<<((iwrdat&0xff0000)>>16)<<"/"
      <<dec<<((((iwrdat&0xff00)>>8)*100)+(iwrdat&0xff))<<endl;
  return iwrdat;
}

bool PixelFEDInterface::loadFedIDRegister() {
  if(Printlevel&1)cout<<"Load FEDID register from DB 0x"<<hex<<pixelFEDCard.fedNumber<<dec<<endl;
  return setFedIDRegister(pixelFEDCard.fedNumber);
}

bool PixelFEDInterface::setFedIDRegister(const uint32_t value) {
  cout<<"Set FEDID register "<<hex<<value<<dec<<endl;
  // write here
  uint32_t got = getFedIDRegister();
  if (value != got) cout<<"soft FEDID = "<<value<<" doesn't match hard board FEDID = "<<got<<endl;
  return value == got;
}

uint32_t PixelFEDInterface::getFedIDRegister() {
  return 0;
}

bool PixelFEDInterface::loadControlRegister() {
  if(Printlevel&1)cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" Load Control register from DB 0x"<<hex<<pixelFEDCard.Ccntrl<<dec<<endl;
  return setControlRegister(pixelFEDCard.Ccntrl);
}

bool PixelFEDInterface::setControlRegister(uint32_t value) {
  if(Printlevel&1)cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" Set Control register "<<hex<<value<<dec<<endl;
  // write here
  pixelFEDCard.Ccntrl=value; // stored this value   
  return false;
}

uint32_t PixelFEDInterface::getControlRegister() {
  return 0;
}

bool PixelFEDInterface::loadModeRegister() {
  if(Printlevel&1)cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" Load Mode register from DB 0x"<<hex<<pixelFEDCard.Ccntrl<<dec<<endl;
  return setModeRegister(pixelFEDCard.modeRegister);
}

bool PixelFEDInterface::setModeRegister(uint32_t value) {
  if(Printlevel&1)cout<<"FEDID:"<<pixelFEDCard.fedNumber<<" Set Mode register "<<hex<<value<<dec<<endl;
  // write here
  pixelFEDCard.modeRegister=value; // stored this value   
  return false;
}

uint32_t PixelFEDInterface::getModeRegister() {
  return 0;
}

void PixelFEDInterface::set_PrivateWord(uint32_t pword) {
}

void PixelFEDInterface::resetSlink() {
}

bool PixelFEDInterface::checkFEDChannelSEU() {
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

void PixelFEDInterface::incrementSEUCountersFromEnbableBits(vector<int> &counter, bitset<48> current, bitset<48> last) {
  for(size_t i = 0; i < current.size(); i++)
    if (current[i] != last[i])
      counter[i]++;
}

bool PixelFEDInterface::checkSEUCounters(int threshold) {
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

void PixelFEDInterface::resetEnbableBits() {
  // Get the current values of higher bits in these registers, so we can leave them alone

  //uint64_t OtherConfigBits = 0;
  //uint64_t enbable_exp = 0; //enbable_expected.to_ullong(); 
  //for (int i = 0; i < 48; ++i)
  //  if (enbable_expected[i])
  //    enbable_exp |= 1<<i;
  //uint64_t write = OtherConfigBits | enbable_exp;
  //  write;
}

void PixelFEDInterface::storeEnbableBits() {
  enbable_expected = pixelFEDCard.cntrl;
  enbable_last = enbable_expected;
}

void PixelFEDInterface::resetSEUCountAndDegradeState(void) {
  cout << "reset SEU counters and the runDegrade flag " << endl;
  // reset the state back to running 
  runDegraded_ = false;
  // clear the count flag
  num_SEU.assign(48, 0);
  // reset the expected state to default
  storeEnbableBits();
}

void PixelFEDInterface::testTTSbits(uint32_t data, int enable) {
  //will turn on the test bits indicate in bits 0...3 as long as bit 31 is set
  //As of this writing, the bits indicated are: 0(Warn), 1(OOS), 2(Busy), 4(Ready)
  //Use a 1 or any >1 to enable, a 0 or <0 to disable
  if (enable>0)
    data = (data | 0x80000000) & 0x8000000f;
  else
    data = data & 0xf;
  // write here
}

void PixelFEDInterface::setXY(int X, int Y) {
}

int PixelFEDInterface::getXYCount() {
  return 0;
}

void PixelFEDInterface::resetXYCount() {
}

int PixelFEDInterface::getNumFakeEvents() {
  return 0;
}

void PixelFEDInterface::resetNumFakeEvents() {
}

uint32_t PixelFEDInterface::readEventCounter() {
  return 0;
}

uint32_t PixelFEDInterface::getFifoStatus() {
  return 0;
}

uint32_t PixelFEDInterface::linkFullFlag() {
  return 0;
}

uint32_t PixelFEDInterface::numPLLLocks() {
  return 0;
}

uint32_t PixelFEDInterface::getFifoFillLevel() {
  return 0;
}

uint64_t PixelFEDInterface::getSkippedChannels() {
  return 0;
}

uint32_t PixelFEDInterface::getErrorReport(int ch) {
  return 0;
}

uint32_t PixelFEDInterface::getTimeoutReport(int ch) {
  return 0;
}

int PixelFEDInterface::TTCRX_I2C_REG_READ(int Register_Nr) {
  return 0;
}

int PixelFEDInterface::TTCRX_I2C_REG_WRITE(int Register_Nr, int Value) {
  return 0;
}
