/*
 * ch_migration.c: Cloud Hypervisor migration support
 *
 * Copyright (C) 2021 Wei Liu <liuwe@microsoft.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library.  If not, see
 * <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include "ch_conf.h"
#include "ch_domain.h"
#include "datatypes.h"
#include "virlog.h"
#include "virerror.h"
#include "viralloc.h"
#include "ch_migration.h"
#include "ch_process.h"
#include "virutil.h"
#include "unistd.h"

#define VIR_FROM_THIS VIR_FROM_CH

VIR_LOG_INIT("ch.ch_migration");

typedef struct _chMigrationCookie chMigrationCookie;
typedef chMigrationCookie *chMigrationCookiePtr;
struct _chMigrationCookie {
	/* Nothing for now */
    int unused;
};

static void
chMigrationCookieFree(chMigrationCookiePtr mig)
{
    if (!mig)
        return;

    VIR_FREE(mig);
}


static chMigrationCookiePtr
chMigrationCookieNew(virDomainObj *dom)
{
    chMigrationCookie *mig = NULL;

    mig = g_new0(chMigrationCookie, 1);


    /* Nothing to do */
    (void)dom;

    return mig;

}

static int
chMigrationBakeCookie(chMigrationCookiePtr mig, char **cookieout,
                      int *cookieoutlen)
{
    g_auto(virBuffer) buf = VIR_BUFFER_INITIALIZER;

    if (!cookieout || !cookieoutlen)
        return 0;

    /* Nothing to do */
    (void)mig;

    *cookieout = NULL;
    *cookieoutlen = 0;

    VIR_DEBUG("cookielen=%d cookie=%s", *cookieoutlen, *cookieout);

    return 0;
}

static int
chMigrationEatCookie(const char *cookiein, int cookieinlen,
                     chMigrationCookiePtr *migout)
{
    chMigrationCookie *mig = NULL;

    mig = g_new0(chMigrationCookie, 1);

    /* Nothing to do */
    (void)cookiein;
    (void)cookieinlen;
    *migout = mig;

    return 0;
}

static bool
chDomainMigrationIsAllowed(virDomainDef *def)
{
    if (def->nhostdevs > 0) {
        virReportError(VIR_ERR_OPERATION_INVALID, "%s",
                _("domain has assigned host devices"));
        return false;
    }

    return true;
}

char *
chDomainMigrationSrcBegin(virConnectPtr conn,
                          virDomainObj *vm,
                          const char *xmlin,
                          char **cookieout,
                          int *cookieoutlen)
{
    virCHDriver *driver = conn->privateData;
    char *xml = NULL;
    chMigrationCookiePtr mig = NULL;
    virDomainDef *tmpdef = NULL, *def;

    if (virCHDomainObjBeginJob(vm, VIR_JOB_MODIFY) < 0)
        goto cleanup;

    if (!(mig = chMigrationCookieNew(vm)))
        goto endjob;

    if (chMigrationBakeCookie(mig, cookieout, cookieoutlen) < 0)
        goto endjob;

    if (xmlin) {
        if (!(tmpdef = virDomainDefParseString(xmlin,
                        driver->xmlopt,
                        NULL,
                        VIR_DOMAIN_DEF_PARSE_INACTIVE |
                        VIR_DOMAIN_DEF_PARSE_SKIP_VALIDATE)))
            goto endjob;

        def = tmpdef;
    } else {
        def = vm->def;
    }

    if (!chDomainMigrationIsAllowed(def))
        goto endjob;

    xml = virDomainDefFormat(def, driver->xmlopt, VIR_DOMAIN_DEF_FORMAT_SECURE);

endjob:
    virCHDomainObjEndJob(vm);

cleanup:
    chMigrationCookieFree(mig);
    virDomainDefFree(tmpdef);
    return xml;
}

virDomainDef *
chDomainMigrationAnyPrepareDef(virCHDriver *driver,
                               const char *dom_xml,
                               const char *dname,
                               char **origname)
{
    virDomainDef *def;
    char *name = NULL;

    if (!dom_xml) {
        virReportError(VIR_ERR_INTERNAL_ERROR, "%s",
                       _("no domain XML passed"));
        return NULL;
    }

    if (!(def = virDomainDefParseString(dom_xml, driver->xmlopt,
                                        NULL,
                                        VIR_DOMAIN_DEF_PARSE_INACTIVE |
                                        VIR_DOMAIN_DEF_PARSE_SKIP_VALIDATE)))
        goto cleanup;

    if (dname) {
        name = def->name;
        def->name = g_strdup(dname);
    }

 cleanup:
    if (def && origname)
        *origname = name;
    else
        VIR_FREE(name);
    return def;
}

int
chDomainMigrationDstPrepare(virConnectPtr dconn,
                            virDomainDef **def,
                            const char *cookiein,
                            int cookieinlen,
                            char **cookieout,
                            int *cookieoutlen,
                            const char *uri_in,
                            char **uri_out,
                            const char *origname)
{
    virCHDriver *driver = dconn->privateData;
    chMigrationCookiePtr mig = NULL;
    virDomainObj *vm = NULL;
    virCommand *cmd;
    virCommand *clh;
    virCommand *chRemote;
    virCommand *socat;
    unsigned short port = 0;
    const char *incFormat;
    g_autofree char *hostname = NULL;
    bool sendDebugMessage = true;
    //FILE *file;

    if (chMigrationEatCookie(cookiein, cookieinlen, &mig) < 0)
        goto cleanup;

    if (!(vm = virDomainObjListAdd(driver->domains, def,
                    driver->xmlopt,
                    VIR_DOMAIN_OBJ_LIST_ADD_LIVE |
                    VIR_DOMAIN_OBJ_LIST_ADD_CHECK_LIVE,
                    NULL)))
        goto cleanup;

    if (virCHDomainObjBeginJob(vm, VIR_JOB_MODIFY) < 0)
        goto cleanup;

    (void) def;
    (void) uri_in;
    (void) uri_out;
    (void) cookieout;
    (void) cookieoutlen;
    (void) origname;


    //get uri_out
    if (virPortAllocatorAcquire(driver->migrationPorts, &port) < 0)
         goto cleanup;
    if ((hostname = virGetHostname()) == NULL)
        goto cleanup;
    incFormat = "%s:%s:%d";
    *uri_out = g_strdup_printf(incFormat, "tcp", hostname, port);
    VIR_DEBUG("Generated uri_out=%s", *uri_out);

    // if (virCHProcessStart(driver, vm, 0, VIR_CH_PROCESS_START_PAUSED) < 0)
    //     goto cleanup;


    //clh command
    cmd = virCommandNew("cloud-hypervisor");
    virCommandAddArgPair(cmd, "--api-socket", "/var/run/libvirt/ch/ch_impish_nfs-socket");
    virCommandAddArg(cmd, "-vvv");
    virCommandAddArgPair(cmd, "--log-file", "/var/log/libvirt/ch.log");
    VIR_DEBUG("PPK: clh cmd : %s", virCommandToString(cmd, false) );
    clh = cmd;

    //ch-remote command
    cmd = virCommandNew("ch-remote");
    virCommandAddArgPair(cmd, "--api-socket","/var/run/libvirt/ch/ch_impish_nfs-socket");
    virCommandAddArg(cmd, "receive-migration");
    virCommandAddArg(cmd, "unix:/var/run/libvirt/ch/ch_impish_nfs-migr-recv");
    VIR_DEBUG("PPK: ch-remote cmd : %s", virCommandToString(cmd, false) );
    chRemote = cmd;

    //socat command
    cmd = virCommandNew("socat");
    virCommandAddArg(cmd, "-d");
    virCommandAddArg(cmd, "-d");
    virCommandAddArg(cmd, "-d");
    virCommandAddArg(cmd, "-d");
    virCommandAddArg(cmd, "-lf");
    virCommandAddArg(cmd, "/var/log/libvirt/socat_dest.log");
    virCommandAddArg(cmd, "-x");
    virCommandAddArg(cmd, "-v");
    virCommandAddArg(cmd, "2>");
    virCommandAddArg(cmd, "/var/log/libvirt/socat_err.log");
    virCommandAddArg(cmd, g_strdup_printf("TCP-LISTEN:%d,reuseaddr", port));
    virCommandAddArg(cmd, "UNIX-CLIENT:/var/run/libvirt/ch/ch_impish_nfs-migr-recv");
    VIR_DEBUG("PPK: socat cmd : %s", virCommandToString(cmd, false) );
    socat = cmd;

    //run commands
    if (virCommandRunAsync(clh, NULL) < 0)
        return -1;
    sleep(5);
    if (virCommandRunAsync(chRemote, NULL) < 0)
        return -1;
    //sleep(5);

    while(access("/var/run/libvirt/ch/ch_impish_nfs-migr-recv", F_OK) != 0){
        if(sendDebugMessage){
            VIR_DEBUG("t-kasanchez waiting on socket");
            sendDebugMessage = false;
        }
    }
    VIR_DEBUG("t-kasanchez socket found");

    if (virCommandRunAsync(socat, NULL) < 0)
        return -1;



    virCHDomainObjEndJob(vm);

    chMigrationCookieFree(mig);




    return 0;

cleanup:
    /* Remove virDomainObj from domain list */
    if (vm)
        virDomainObjListRemove(driver->domains, vm);
    chMigrationCookieFree(mig);

    return -1;
}


int
chDomainMigrationSrcPerform(virCHDriver *driver,
                            virDomainObj *vm,
                            virDomainDef **def,
                            const char *dom_xml,
                            const char *dconnuri,
                            const char *uri_str,
                            const char *dname,
                            unsigned int flags)
{
    virCommand *cmd;
    bool sendDebugMessage = true;
    (void) def;
    (void) driver;
    (void) vm;
    (void) dom_xml;
    (void) dconnuri;
    (void) uri_str;
    (void) dname;
    (void) flags;

    // if (!(vm = virDomainObjListAdd(driver->domains, def,
    //                                 driver->xmlopt,
    //                                 VIR_DOMAIN_OBJ_LIST_ADD_LIVE |
    //                                 VIR_DOMAIN_OBJ_LIST_ADD_CHECK_LIVE,
    //                                 NULL)))
    //     return -1;

    // if (virCHDomainObjBeginJob(vm, VIR_JOB_MODIFY) < 0)
    //     return -1;

    VIR_DEBUG("t-kasanchez %s", uri_str);
    cmd = virCommandNew("socat");
    virCommandAddArg(cmd, "-d");
    virCommandAddArg(cmd, "-d");
    virCommandAddArg(cmd, "-d");
    virCommandAddArg(cmd, "-d");
    virCommandAddArg(cmd, "-b1024");
    virCommandAddArg(cmd, "-lf");
    virCommandAddArg(cmd, "/var/log/libvirt/socat_src.log");
    virCommandAddArg(cmd, "-x");
    virCommandAddArg(cmd, "-v");
    virCommandAddArg(cmd, "2>");
    virCommandAddArg(cmd, "/var/log/libvirt/socat_err.log");
    virCommandAddArg(cmd, "UNIX-LISTEN:/var/run/libvirt/ch/ch_impish_nfs-migr-send,reuseaddr");
    virCommandAddArg(cmd, uri_str);
    if (virCommandRunAsync(cmd, NULL) < 0)
        return -1;

    while(access("/var/run/libvirt/ch/ch_impish_nfs-migr-send", F_OK) != 0){
        if(sendDebugMessage){
            VIR_DEBUG("t-kasanchez waiting on socket");
            sendDebugMessage = false;
        }
    }
    VIR_DEBUG("t-kasanchez socket found");

    //sleep(5);
    cmd = virCommandNew("ch-remote");
    virCommandAddArgPair(cmd, "--api-socket", "/var/run/libvirt/ch/ch_impish_nfs-socket");
    virCommandAddArg(cmd, "send-migration");
    virCommandAddArg(cmd, "unix:/var/run/libvirt/ch/ch_impish_nfs-migr-send");
    if (virCommandRun(cmd, NULL) < 0)
        return -1;

    // virCHDomainObjEndJob(vm);

    return 0;


}


// virDomainPtr
// chDomainMigrationDstFinish(virCHDriver *driver,
//                            virConnectPtr dconn,
//                            virDomainObj *vm,
//                            unsigned int flags,
//                            int cancelled)
// {
//     virDomainPtr dom = NULL;
//     (void) driver;
//     (void) dconn;
//     (void) vm;
//     (void) flags;
//     (void) cancelled;
//     return dom;
//     dom = virGetDomain(dconn, vm->def->name, vm->def->uuid, vm->def->id);

// }


int
chDomainMigrationSrcConfirm(virCHDriver *driver,
                            virDomainObj *vm,
                            unsigned int flags,
                            int cancelled)
{
    (void) driver;
    (void) vm;
    (void) flags;
    (void) cancelled;

    return -1;
}
