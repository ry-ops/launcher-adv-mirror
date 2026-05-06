| Board | partition scheme | environments |
| ---   | ---              | ---          |
| esp32-4mb | support_files/custom_4Mb_noOta.csv | "CYD-2432S022C", "CYD-2432S032C", "CYD-2432S032R", "CYD-3248S035C", "CYD-3248S035R", "elecrow-24B", "elecrow-28B", "elecrow-35B", "elecrow-35Bv2_2", "headless-esp32-4mb" |
| esp32-4mb | support_files/custom_4Mb.csv | "CYD-2432W328R", "CYD-2432S024R", "CYD-2432W328C_2", "CYD-2432W328C", "CYD-2-USB", "CYD-2432S028", "m5stack-cplus1_1", "m5stack-c", "Marauder-Mini", "Marauder-v7", "Marauder-v4-OG", "Marauder-v61", "WaveSentry-R1", "Phantom_S024R" |
| esp32-4mb | support_files/custom_4Mbcore.csv | "m5stack-core-4Mb" |
| esp32-8mb | support_files/custom_8Mb.csv | "headless-esp32-8mb", "m5stack-cplus2" |
| esp32-16mb | support_files/custom_16Mb.csv | "m5stack-core", "m5stack-core2", "Awok-Mini", "Awok-Touch" |
| esp32-16mb-psram | support_files/custom_16Mb.csv | "m5stack-paper" |
| ESP32S3-4M | support_files/custom_4Mb_noOta.csv | "headless-esp32s3-4mb" |
| esp32s3-8mb | support_files/custom_8Mb.csv | "m5stack-cardputer" |
| esp32s3-8mb | support_files/custom_8Mb2.csv | "m5stack-dinmeter" |
| esp32s3-8mb-psram | support_files/custom_8Mb2.csv | "m5stack-sticks3" |
| esp32s3-8mb-psram | support_files/custom_8Mb.csv | "headless-esp32s3-8mb" |
| esp32s3-16mb-psram | support_files/custom_16Mb.csv | "CYD-8048S043C", "CYD-8048W550C", "CYD-3248W535C", "CYD-4827S043R", "headless-esp32s3-16mb", "lilygo-t-deck", "lilygo-t-deck-plus", "lilygo-t-deck-pro","lilygo-t-display-S3-pro", "lilygo-t-display-S3-touch", "lilygo-t-dongle-s3-tft", "lilygo-t-embed", "lilygo-t-embed-cc1101", "lilygo-t-hmi", "lilygo-t-lora-pager", "lilygo-t-watch-s3", "lilygo-t-watch-ultra", "lilygo-t5-epaper-s3-pro", "m5stack-cores3", "m5stack-paper-s3", "smoochiee-board", "waveshare-esp32-s3-lcd-147", "lilygo-t-display-S3-amoled" |
| esp32c6-16mb | support_files/custom_16Mb.csv | "arduino-nesso-n1" |
| esp32p4-16mb-psram | support_files/custom_16Mb_p4.csv | "m5stack-tab5" |


## Test command for build
```
python support_files/build.py --board <board> --debug-output <output file> --env <env1> <env2> <env3> ... <envN>


python support_files/build.py --board esp32-4mb --debug-output output.txt --env CYD-2432W328C CYD-2432S024R CYD-2432W328C_2 CYD-2432W328R CYD-2-USB CYD-2432S028 m5stack-cplus1_1 m5stack-c Marauder-Mini Marauder-v7 Marauder-v4-OG Marauder-v61 WaveSentry-R1 Phantom_S024R
```
