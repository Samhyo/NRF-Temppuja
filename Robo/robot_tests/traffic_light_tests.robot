# robot C:\sulautettu_ohjelmistokehitys\automaatiotestaus\src\serial_str.robot


*** Settings ***
Library    SerialLibrary
Library    String

*** Variables ***
${com}        COM8          # Vaihda oma portti
${baud}       115200
${board}      nRF
${ok_seq}     T000120       # Testisyöte oikea
${err_seq}    T00106A       # Testisyöte virheellinen
${ok_resp}    80
${err_resp}   -6             # TIME_PARSE_NONDIGIT_ERROR koodisi mukaan

*** Test Cases ***
Connect Serial
    Log To Console  Connecting to ${board}
    Add Port  ${com}  baudrate=${baud}  encoding=ascii
    Port Should Be Open  ${com}
    Reset Input Buffer
    Reset Output Buffer

Valid Time String
    Reset Input Buffer
    Reset Output Buffer
    Write Data   ${ok_seq}   encoding=ascii
    Sleep   0.5s
    ${read}=   Read Until   terminator=\n   encoding=ascii   timeout=2s
    Log To Console   Received: ${read}
    Should Contain   ${read}    ${ok_resp}

Invalid Time String
    Reset Input Buffer
    Reset Output Buffer
    Write Data   ${err_seq}   encoding=ascii
    Sleep   0.5s
    ${read}=   Read Until   terminator=\n   encoding=ascii   timeout=2s
    Log To Console   Received: ${read}
    Should Contain   ${read}    ${err_resp}

Disconnect Serial
    Log To Console  Disconnecting ${board}
    [Teardown]  Delete Port  ${com}
