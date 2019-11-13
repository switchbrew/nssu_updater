// Include the most common headers from the C standard library
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <arpa/inet.h>

// Include the main libnx system header, for Switch development
#include <switch.h>

// Main program entrypoint
int main(int argc, char* argv[])
{
    Result rc=0;
    NsSystemUpdateControl sucontrol={0};
    AsyncResult asyncres={0};
    u32 state=0;
    bool tmpflag=0;
    bool sleepflag=0;
    Result sleeprc=0;
    u32 updatetype=0;
    u32 ipaddr = ntohl(__nxlink_host.s_addr); // TODO: Should specifiying ipaddr via other means be supported?

    appletLockExit();

    sleeprc = appletIsAutoSleepDisabled(&sleepflag);
    if (R_SUCCEEDED(sleeprc)) sleeprc = appletSetAutoSleepDisabled(true);

    consoleInit(NULL);

    printf("nssu_updater\n");
    printf("Press A to install update with nssuControlSetupCardUpdate.\n");
    printf("Press B to install update with nssuControlSetupCardUpdateViaSystemUpdater.\n");
    if (ipaddr) {
        printf("Press X to Send the sysupdate.\n");
        printf("Press Y to Receive the sysupdate.\n");
    }
    printf("Press + exit, aborting any operations prior to the final stage.\n");

    rc = nssuInitialize();
    printf("nssuInitialize(): 0x%x\n", rc);

    if (ipaddr) printf("Using IP addr from nxlink: %s\n", inet_ntoa(__nxlink_host));

    u32 cnt=0;

    // Main loop
    while (appletMainLoop())
    {
        // Scan all the inputs. This should be done once for each frame
        hidScanInput();

        // hidKeysDown returns information about which buttons have been
        // just pressed in this frame compared to the previous one
        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_PLUS)
            break; // break in order to return to hbmenu

        if (R_SUCCEEDED(rc)) {
            if (state==0 && R_SUCCEEDED(rc) && ((kDown & (KEY_A|KEY_B)) || (ipaddr && (kDown & (KEY_X|KEY_Y))))) {
                if (kDown & (KEY_A|KEY_B|KEY_Y)) {
                    rc = nssuOpenSystemUpdateControl(&sucontrol);
                    printf("nssuOpenSystemUpdateControl(): 0x%x\n", rc);
                }

                if (kDown & (KEY_A|KEY_B)) {
                    updatetype = 0;
                    if (R_SUCCEEDED(rc)) {
                        if (kDown & KEY_A) {
                            rc = nssuControlSetupCardUpdate(&sucontrol, NULL, NSSU_CARDUPDATE_TMEM_SIZE_DEFAULT);
                            printf("nssuControlSetupCardUpdate(): 0x%x\n", rc);
                        }
                        else if (kDown & KEY_B) {
                            rc = nssuControlSetupCardUpdateViaSystemUpdater(&sucontrol, NULL, NSSU_CARDUPDATE_TMEM_SIZE_DEFAULT);
                            printf("nssuControlSetupCardUpdateViaSystemUpdater(): 0x%x\n", rc);
                        }
                    }

                    if (R_SUCCEEDED(rc)) {
                        rc = nssuControlHasPreparedCardUpdate(&sucontrol, &tmpflag);
                        printf("nssuControlHasPreparedCardUpdate(): 0x%x, %d\n", rc, tmpflag);
                        if (R_SUCCEEDED(rc) && tmpflag) {
                            printf("Update was already Prepared, aborting.\n");
                            rc = 1;
                        }
                    }

                    if (R_SUCCEEDED(rc)) {
                        rc = nssuControlRequestPrepareCardUpdate(&sucontrol, &asyncres);
                        printf("nssuControlRequestPrepareCardUpdate(): 0x%x\n", rc);
                    }
                }
                else if (kDown & (KEY_X|KEY_Y)) {
                    updatetype=1;

                    NsSystemDeliveryInfo deliveryinfo={0};
                    rc = nsInitialize();
                    if (R_FAILED(rc)) printf("nsInitialize(): 0x%x\n", rc);

                    if (R_SUCCEEDED(rc)) {
                        rc = nsGetSystemDeliveryInfo(&deliveryinfo);
                        printf("nsGetSystemDeliveryInfo(): 0x%x\n", rc);

                        nsExit();
                    }

                    if (R_SUCCEEDED(rc) && (kDown & KEY_Y)) {
                        // TODO: Attempt to load/generate this from elsewhere first?
                        FILE *f = fopen("nssu_updater_SystemDeliveryInfo.bin", "rb");
                        if (f) {
                            fread(&deliveryinfo, 1, sizeof(deliveryinfo), f);
                            fclose(f);
                        }
                    }

                    if (R_SUCCEEDED(rc) && (kDown & KEY_X)) {
                        rc = nssuRequestSendSystemUpdate(&asyncres, ipaddr, 55556, &deliveryinfo);
                        printf("nssuRequestSendSystemUpdate(): 0x%x\n", rc);
                    }
                    else if (R_SUCCEEDED(rc) && (kDown & KEY_Y)) {
                        rc = nssuControlRequestReceiveSystemUpdate(&sucontrol, &asyncres, ipaddr, 55556, &deliveryinfo);
                        printf("nssuControlRequestReceiveSystemUpdate(): 0x%x\n", rc);
                        updatetype=2;
                    }
                }

                if (R_SUCCEEDED(rc)) state=1;
            }
            else if(state==1 && R_SUCCEEDED(rc)) {
                NsSystemUpdateProgress progress={0};
                if (updatetype==0)
                    rc = nssuControlGetPrepareCardUpdateProgress(&sucontrol, &progress);
                else if (updatetype==1)
                    rc = nssuGetSendSystemUpdateProgress(&progress);
                else if (updatetype==2)
                    rc = nssuControlGetReceiveProgress(&sucontrol, &progress);
                consoleClear();
                float percent = 0.0f;
                if (progress.total_size > 0) percent = (((float)progress.current_size) / ((float)progress.total_size)) * 100.0f;
                if (percent > 100.0f) percent = 100.0f;
                printf("Get*UpdateProgress(): 0x%x, 0x%lx of 0x%lx, %f%%\n", rc, progress.current_size, progress.total_size, percent);

                cnt++;
                if (cnt>=60) {
                    for (u32 cnti=0; cnti<cnt/60; cnti++) printf(".");
                    printf("\n");
                }
                if (cnt >= 60*10) cnt=0;
                consoleUpdate(NULL);

                if (R_SUCCEEDED(rc)) {
                    Result rc2 = asyncResultWait(&asyncres, 0);
                    if (R_SUCCEEDED(rc2)) {
                        printf("Operation finished.\n");

                        printf("asyncResultGet...\n");
                        consoleUpdate(NULL);
                        rc = asyncResultGet(&asyncres);
                        printf("asyncResultGet(): 0x%x\n", rc);
                        consoleUpdate(NULL);

                        if (R_SUCCEEDED(rc)) {
                            printf("asyncResultClose...\n");
                            consoleUpdate(NULL);
                            asyncResultClose(&asyncres);
                        }
                    }

                    if (R_SUCCEEDED(rc2))  {
                        // TODO: Support the other updatetypes.
                        if (R_SUCCEEDED(rc) && updatetype==0) {
                            rc = nssuControlHasPreparedCardUpdate(&sucontrol, &tmpflag);
                            printf("nssuControlHasPreparedCardUpdate(): 0x%x, %d\n", rc, tmpflag);
                            if (R_SUCCEEDED(rc) && !tmpflag) {
                                printf("Update was not Prepared, aborting.\n");
                                rc = 1;
                            }
                            consoleUpdate(NULL);
                        }

                        if (R_SUCCEEDED(rc) && updatetype==0) {
                            printf("Applying update...\n");
                            consoleUpdate(NULL);
                            rc = nssuControlApplyCardUpdate(&sucontrol);
                            printf("nssuControlApplyCardUpdate(): 0x%x\n", rc);
                        }

                        if (R_SUCCEEDED(rc)) {
                            printf("The update has finished. Press + to exit (and reboot).\n");
                            state=2;
                        }
                     }
                }
            }
        }

        // Update the console, sending a new frame to the display
        consoleUpdate(NULL);
    }

    printf("asyncResultClose...\n");
    consoleUpdate(NULL);
    asyncResultClose(&asyncres);

    nssuControlClose(&sucontrol);
    nssuExit();

    if (state==2 && updatetype==0) {
        printf("Rebooting...\n");
        consoleUpdate(NULL);
        rc = appletRequestToReboot();
        printf("appletRequestToReboot(): 0x%x\n", rc);
        consoleUpdate(NULL);
    }

    // Deinitialize and clean up resources used by the console (important!)
    consoleExit(NULL);
    if (R_SUCCEEDED(sleeprc)) sleeprc = appletSetAutoSleepDisabled(sleepflag);
    appletUnlockExit();
    return 0;
}
