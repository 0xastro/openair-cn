/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the Apache License, Version 2.0  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*****************************************************************************
  Source      Detach.c

  Version     0.1

  Date        2013/05/07

  Product     NAS stack

  Subsystem   EPS Mobility Management

  Author      Frederic Maurel

  Description Defines the detach related EMM procedure executed by the
        Non-Access Stratum.

        The detach procedure is used by the UE to detach for EPS servi-
        ces, to disconnect from the last PDN it is connected to; by the
        network to inform the UE that it is detached for EPS services
        or non-EPS services or both, to disconnect the UE from the last
        PDN to which it is connected and to inform the UE to re-attach
        to the network and re-establish all PDN connections.

*****************************************************************************/

#include "log.h"
#include "msc.h"
#include "dynamic_memory_check.h"
#include "emmData.h"
#include "emm_proc.h"
#include "emm_sap.h"
#include "esm_sap.h"
#include "nas_itti_messaging.h"


/****************************************************************************/
/****************  E X T E R N A L    D E F I N I T I O N S  ****************/
/****************************************************************************/

/****************************************************************************/
/*******************  L O C A L    D E F I N I T I O N S  *******************/
/****************************************************************************/

/* String representation of the detach type */
static const char                      *_emm_detach_type_str[] = {
  "EPS", "IMSI", "EPS/IMSI",
  "RE-ATTACH REQUIRED", "RE-ATTACH NOT REQUIRED", "RESERVED"
};

/*
 *  Detach Proc: Timer handler
 */
static void *_detach_t3422_handler (void *);

typedef struct {
  unsigned int                            ue_id;
#define DETACH_REQ_COUNTER_MAX  5
  unsigned int                            retransmission_count;
} nw_detach_data_t;

/****************************************************************************
 **                                                                        **
 ** Name:    _detach_t3422_handler()                                       **
 **                                                                        **
 ** Description: T3422 timeout handler                                     **
 **      Upon T3422 timer expiration, the Detach request                   **
 **      message is retransmitted and the timer restarted. When            **
 **      retransmission counter is exceed, the MME shall abort the         **
 **      detach procedure and perform implicit detach.                     **
 **                                                                        **
 **                                                                        **
 ** Inputs:  args:      handler parameters                                 **
 **                                                                        **
 **                                                                        **
 ** Outputs:     None                                                      **
 **      Return:    None                                                   **
 **      Others:    None                                                   **
 **                                                                        **
 ***************************************************************************/
static void *
_detach_t3422_handler (
  void *args)
{
  OAILOG_FUNC_IN (LOG_NAS_EMM);
  nw_detach_data_t                  *data = (nw_detach_data_t *) (args);

  DevAssert(data);

  mme_ue_s1ap_id_t ue_id = data->ue_id;

  /*
   * Increment the retransmission counter
   */
  data->retransmission_count += 1;
  OAILOG_WARNING (LOG_NAS_EMM, "EMM-PROC: T3422 timer expired,retransmission " "counter = %d\n",
      data->retransmission_count);

  if (data->retransmission_count < DETACH_REQ_COUNTER_MAX) {
    /*
     * Resend detach request message to the UE
     */
    emm_proc_nw_initiated_detach_request (ue_id);
  } else {

    /*
     * Abort the detach procedure and perform implict detach
     */
    if (data) {
      // free timer argument
      free_wrapper ((void **) &data);
      emm_data_context_t *emm_ctx = emm_data_context_get (&_emm_data, ue_id);
      DevAssert(emm_ctx);
      emm_ctx->t3422_arg = NULL;
    }
    emm_proc_detach_request (ue_id, EMM_DETACH_TYPE_EPS, 0, 0, 0, 0, 0, 0);
  }
  OAILOG_FUNC_RETURN (LOG_NAS_EMM, NULL);
}

void
_clear_emm_ctxt(emm_data_context_t *emm_ctx) {

  if (!emm_ctx) {
    return;
  }

  emm_data_context_stop_all_timers(emm_ctx);

  esm_sap_t                               esm_sap = {0};
  /*
   * Release ESM PDN and bearer context
   */

  esm_sap.primitive = ESM_EPS_BEARER_CONTEXT_DEACTIVATE_REQ;
  esm_sap.ue_id = emm_ctx->ue_id;
  esm_sap.ctx = emm_ctx;
  esm_sap.data.eps_bearer_context_deactivate.ebi = ESM_SAP_ALL_EBI;
  esm_sap_send (&esm_sap);

  if (emm_ctx->esm_msg) {
    bdestroy(emm_ctx->esm_msg);
  }

  // Change the FSM state to Deregistered
  if (emm_fsm_get_status (emm_ctx->ue_id, emm_ctx) != EMM_DEREGISTERED) {
    emm_fsm_set_status (emm_ctx->ue_id, emm_ctx, EMM_DEREGISTERED);
  }

  /*
   * Release the EMM context
   */
  emm_data_context_remove(&_emm_data, emm_ctx);
  free_wrapper((void **) &emm_ctx);
}



/*
   --------------------------------------------------------------------------
        Internal data handled by the detach procedure in the UE
   --------------------------------------------------------------------------
*/


/*
   --------------------------------------------------------------------------
        Internal data handled by the detach procedure in the MME
   --------------------------------------------------------------------------
*/


/****************************************************************************/
/******************  E X P O R T E D    F U N C T I O N S  ******************/
/****************************************************************************/

/*
   --------------------------------------------------------------------------
            Detach procedure executed by the UE
   --------------------------------------------------------------------------
*/

/*
   --------------------------------------------------------------------------
            Detach procedure executed by the MME
   --------------------------------------------------------------------------
*/
/****************************************************************************
 **                                                                        **
 ** Name:    emm_proc_detach()                                         **
 **                                                                        **
 ** Description: Initiate the detach procedure to inform the UE that it is **
 **      detached for EPS services, or to re-attach to the network **
 **      and re-establish all PDN connections.                     **
 **                                                                        **
 **              3GPP TS 24.301, section 5.5.2.3.1                         **
 **      In state EMM-REGISTERED the network initiates the detach  **
 **      procedure by sending a DETACH REQUEST message to the UE,  **
 **      starting timer T3422 and entering state EMM-DEREGISTERED- **
 **      INITIATED.                                                **
 **                                                                        **
 ** Inputs:  ue_id:      UE lower layer identifier                  **
 **      type:      Type of the requested detach               **
 **      Others:    _emm_detach_type_str                       **
 **                                                                        **
 ** Outputs:     None                                                      **
 **      Return:    RETURNok, RETURNerror                      **
 **      Others:    T3422                                      **
 **                                                                        **
 ***************************************************************************/
int
emm_proc_detach (
  mme_ue_s1ap_id_t ue_id,
  emm_proc_detach_type_t type)
{
  OAILOG_FUNC_IN (LOG_NAS_EMM);
  int                                     rc = RETURNerror;

  OAILOG_INFO (LOG_NAS_EMM, "EMM-PROC  - Initiate detach type = %s (%d)", _emm_detach_type_str[type], type);
  /*
   * TODO
   */
  OAILOG_FUNC_RETURN (LOG_NAS_EMM, rc);
}

/****************************************************************************
 **                                                                        **
 ** Name:    emm_proc_detach_request()                                 **
 **                                                                        **
 ** Description: Performs the UE initiated detach procedure for EPS servi- **
 **      ces only When the DETACH REQUEST message is received by   **
 **      the network.                                              **
 **                                                                        **
 **              3GPP TS 24.301, section 5.5.2.2.2                         **
 **      Upon receiving the DETACH REQUEST message the network     **
 **      shall send a DETACH ACCEPT message to the UE and store    **
 **      the current EPS security context, if the detach type IE   **
 **      does not indicate "switch off". Otherwise, the procedure  **
 **      is completed when the network receives the DETACH REQUEST **
 **      message.                                                  **
 **      The network shall deactivate the EPS bearer context(s)    **
 **      for this UE locally without peer-to-peer signalling and   **
 **      shall enter state EMM-DEREGISTERED.                       **
 **                                                                        **
 ** Inputs:  ue_id:      UE lower layer identifier                  **
 **      type:      Type of the requested detach               **
 **      switch_off:    Indicates whether the detach is required   **
 **             because the UE is switched off or not      **
 **      native_ksi:    true if the security context is of type    **
 **             native                                     **
 **      ksi:       The NAS ket sey identifier                 **
 **      guti:      The GUTI if provided by the UE             **
 **      imsi:      The IMSI if provided by the UE             **
 **      imei:      The IMEI if provided by the UE             **
 **      Others:    _emm_data                                  **
 **                                                                        **
 ** Outputs:     None                                                      **
 **      Return:    RETURNok, RETURNerror                      **
 **      Others:    None                                       **
 **                                                                        **
 ***************************************************************************/
int
emm_proc_detach_request (
  mme_ue_s1ap_id_t ue_id,
  emm_proc_detach_type_t type,
  int switch_off,
  ksi_t native_ksi,
  ksi_t ksi,
  guti_t * guti,
  imsi_t * imsi,
  imei_t * imei)
{
  OAILOG_FUNC_IN (LOG_NAS_EMM);
  int                                     rc;
  emm_data_context_t                     *emm_ctx = NULL;

  OAILOG_INFO (LOG_NAS_EMM, "EMM-PROC  - Detach type = %s (%d) requested (ue_id=" MME_UE_S1AP_ID_FMT ")", _emm_detach_type_str[type], type, ue_id);
  /*
   * Get the UE context
   */
  emm_ctx = emm_data_context_get (&_emm_data, ue_id);

  if (emm_ctx == NULL) {
    OAILOG_WARNING (LOG_NAS_EMM, "No EMM context exists for the UE (ue_id=" MME_UE_S1AP_ID_FMT ")", ue_id);
    increment_counter ("ue_detach", 1, 2, "result", "failure", "cause", "no_emm_context");
    // There may be MME APP Context. Trigger clean up in MME APP
    nas_itti_detach_req(ue_id);
    OAILOG_FUNC_RETURN (LOG_NAS_EMM, RETURNok);
  }

  if (switch_off) {
    MSC_LOG_EVENT (MSC_NAS_EMM_MME, "0 Removing UE context ue id " MME_UE_S1AP_ID_FMT " ", ue_id);
    increment_counter ("ue_detach", 1, 1, "result", "success");
    increment_counter ("ue_detach", 1, 1, "action", "detach_accept_not_sent");
    rc = RETURNok;
  } else {
    /*
     * Normal detach without UE switch-off
     */
    emm_sap_t                               emm_sap = {0};
    emm_as_data_t                          *emm_as = &emm_sap.u.emm_as.u.data;

    /*
     * Setup NAS information message to transfer
     */
    emm_as->nas_info = EMM_AS_NAS_DATA_DETACH_ACCEPT;
    emm_as->nas_msg = NULL;
    /*
     * Set the UE identifier
     */
    emm_as->ue_id = ue_id;
    /*
     * Setup EPS NAS security data
     */
    emm_as_set_security_data (&emm_as->sctx, &emm_ctx->_security, false, true);
    /*
     * Notify EMM-AS SAP that Detach Accept message has to
     * be sent to the network
     */
    emm_sap.primitive = EMMAS_DATA_REQ;
    rc = emm_sap_send (&emm_sap);
    increment_counter ("ue_detach", 1, 1, "result", "success");
    increment_counter ("ue_detach", 1, 1, "action", "detach_accept_sent");
  }
  if (rc != RETURNerror) {

    emm_sap_t                               emm_sap = {0};

    /*
     * Notify EMM FSM that the UE has been implicitly detached
     */
    MSC_LOG_TX_MESSAGE (MSC_NAS_EMM_MME, MSC_NAS_EMM_MME, NULL, 0, "0 EMMREG_DETACH_REQ ue id " MME_UE_S1AP_ID_FMT " ", ue_id);
    emm_sap.primitive = EMMREG_DETACH_REQ;
    emm_sap.u.emm_reg.ue_id = ue_id;
    emm_sap.u.emm_reg.ctx = emm_ctx;
    rc = emm_sap_send (&emm_sap);
    // Notify MME APP to trigger Session release towards SGW and S1 signaling release towards S1AP.
    nas_itti_detach_req(ue_id);
  }
  // Release emm and esm context
  _clear_emm_ctxt(emm_ctx);

  OAILOG_FUNC_RETURN (LOG_NAS_EMM, RETURNok);
}

/****************************************************************************
 **                                                                        **
 ** Name:    emm_proc_detach_accept()                                      **
 **                                                                        **
 ** Description: Trigger clean up of UE context in ESM/EMM,MME_APP,SGPW    **
 **      and S1AP                                                          **
 **                                                                        **
 ***************************************************************************/
int
emm_proc_detach_accept (mme_ue_s1ap_id_t ue_id)
{

  OAILOG_FUNC_IN (LOG_NAS_EMM);
  emm_data_context_t                     *emm_ctx = NULL;

  /*
   * Get the UE context
   */
  emm_ctx = emm_data_context_get (&_emm_data, ue_id);

  if (emm_ctx == NULL) {
    OAILOG_WARNING (LOG_NAS_EMM, "No EMM context exists for the UE (ue_id=" MME_UE_S1AP_ID_FMT ")", ue_id);
    // There may be MME APP Context. Trigger clean up in MME APP
    nas_itti_detach_req(ue_id);
    OAILOG_FUNC_RETURN (LOG_NAS_EMM, RETURNok);
  }

  // Stop T3422
  if (emm_ctx->T3422.id != NAS_TIMER_INACTIVE_ID) {
    OAILOG_DEBUG (LOG_NAS_EMM, "EMM-PROC  - Stop timer T3422 (%d) for ue_id %d \n", emm_ctx->T3422.id, ue_id);
    emm_ctx->T3422.id = nas_timer_stop (emm_ctx->T3422.id);
    if (emm_ctx->t3422_arg) {
      free_wrapper (&emm_ctx->t3422_arg);
      emm_ctx->t3422_arg = NULL;
    }
  }

  emm_sap_t                               emm_sap = {0};
  /*
   * Notify EMM FSM that the UE has been detached
   */
  emm_sap.primitive = EMMREG_DETACH_REQ;
  emm_sap.u.emm_reg.ue_id = ue_id;
  emm_sap.u.emm_reg.ctx = emm_ctx;
  emm_sap_send (&emm_sap);
  // Notify MME APP to trigger Session release towards SGW and S1 signaling release towards S1AP.
  nas_itti_detach_req(ue_id);
  // Release emm and esm context
  _clear_emm_ctxt(emm_ctx);

  OAILOG_FUNC_RETURN (LOG_NAS_EMM, RETURNok);
}


/****************************************************************************
 **                                                                        **
 ** Name:    emm_proc_nw_initiated_detach_request()                        **
 **                                                                        **
 ** Description: Performs NW initiated detach procedure by sending         **
 **      DETACH REQUEST message to UE                                      **
 **                                                                        **
 ***************************************************************************/
int
emm_proc_nw_initiated_detach_request (
  mme_ue_s1ap_id_t ue_id)
{
  OAILOG_FUNC_IN (LOG_NAS_EMM);
  int                                     rc;
  emm_data_context_t                     *emm_ctx = NULL;

  OAILOG_INFO (LOG_NAS_EMM, "EMM-PROC  - NW Initiated Detach Requested for the UE (ue_id=" MME_UE_S1AP_ID_FMT ")", ue_id);
  /*
   * Get the UE context
   */
  emm_ctx = emm_data_context_get (&_emm_data, ue_id);

  if (!emm_ctx) {
    OAILOG_WARNING (LOG_NAS_EMM, "No EMM context exists for the UE (ue_id=" MME_UE_S1AP_ID_FMT ")", ue_id);
    OAILOG_FUNC_RETURN (LOG_NAS_EMM, RETURNerror);
  }

  /*
   * Send Detach Request to UE
   */
  emm_sap_t                               emm_sap = {0};
  emm_as_data_t                          *emm_as = &emm_sap.u.emm_as.u.data;

  /*
   * Setup NAS information message to transfer
   */
  emm_as->nas_info = EMM_AS_NAS_DATA_DETACH_REQ;
  emm_as->nas_msg = NULL;
  emm_as->guti = NULL;
  /*
   * Set the UE identifier
   */
  emm_as->ue_id = ue_id;
  /*
   * Setup EPS NAS security data
   */
  emm_as_set_security_data (&emm_as->sctx, &emm_ctx->_security, false, true);
  /*
   * Notify EMM-AS SAP that Detach Request message has to
   * be sent to the network
   */
  emm_sap.primitive = EMMAS_DATA_REQ;
  rc = emm_sap_send (&emm_sap);
  if (rc != RETURNerror) {
    if (emm_ctx->T3422.id != NAS_TIMER_INACTIVE_ID) {
      /*
       * Re-start T3422 timer
       */
      emm_ctx->T3422.id = nas_timer_restart (emm_ctx->T3422.id);
    } else {
      /*
       * Start T3422 timer
       */
      nw_detach_data_t                  *data =  (nw_detach_data_t *) calloc ( 1, sizeof(nw_detach_data_t));
      DevAssert(data);
      data->ue_id = ue_id;
      data->retransmission_count = 0;
      emm_ctx->T3422.id = nas_timer_start (emm_ctx->T3422.sec, _detach_t3422_handler, data);
      emm_ctx->t3422_arg = (void *) data;
    }
 }
  OAILOG_FUNC_RETURN (LOG_NAS_EMM, RETURNok);
}
