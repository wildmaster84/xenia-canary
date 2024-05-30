/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Emulator. All rights reserved.                        *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>

#include "xenia/base/logging.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xsession.h"

DECLARE_bool(upnp);

namespace xe {
namespace kernel {

void Uint64toXNKID(uint64_t sessionID, XNKID* xnkid) {
  uint64_t session_id = xe::byte_swap(sessionID);
  memcpy(xnkid->ab, &session_id, sizeof(XNKID));
}

uint64_t XNKIDtoUint64(XNKID* sessionID) {
  uint64_t session_id = 0;
  memcpy(&session_id, sessionID->ab, sizeof(XNKID));
  return xe::byte_swap(session_id);
}

XSession::XSession(KernelState* kernel_state)
    : XObject(kernel_state, Type::Session) {
  session_id_ = -1;
}

X_STATUS XSession::Initialize() {
  auto native_object = CreateNative(sizeof(X_KSESSION));
  if (!native_object) {
    return X_STATUS_NO_MEMORY;
  }

  auto guest_object = reinterpret_cast<X_KSESSION*>(native_object);
  guest_object->handle = handle();
  // Based on what is in XAM it seems like size of this object is only 4 bytes.
  return X_STATUS_SUCCESS;
}

X_RESULT XSession::CreateSession(uint8_t user_index, uint8_t public_slots,
                                 uint8_t private_slots, uint32_t flags,
                                 uint32_t session_info_ptr,
                                 uint32_t nonce_ptr) {
  if (is_session_created_) {
    // Todo: Find proper code!
    return X_ERROR_FUNCTION_FAILED;
  }

  const xam::UserProfile* user_profile =
      kernel_state_->user_profile((uint32_t)user_index);
  if (!user_profile) {
    return X_ERROR_FUNCTION_FAILED;
  }

  XSESSION_INFO* pSessionInfo =
      kernel_state_->memory()->TranslateVirtual<XSESSION_INFO*>(
          session_info_ptr);

  GenerateExchangeKey(&pSessionInfo->keyExchangeKey);
  PrintSessionType((SessionFlags)flags);

  uint64_t* pNonce =
      kernel_state_->memory()->TranslateVirtual<uint64_t*>(nonce_ptr);
  // CSGO only uses STATS flag to create a session to POST stats pre round.
  // Minecraft and Portal 2 use flags HOST + STATS.
  //
  // Creating a session when not host can cause failure in joining sessions
  // such as L4D2 and Portal 2.
  //
  // Hexic creates a session with SINGLEPLAYER_WITH_STATS (without HOST bit)
  // with contexts
  //
  // Create presence sessions?
  // - Create when joining a session
  // - Explicitly create a presence session (Frogger without HOST bit)
  // Based on Presence flag set?
  if (flags == STATS) {
    CreateStatsSession(pSessionInfo, pNonce, user_index, public_slots,
                       private_slots, flags);
  } else if (HasSessionFlag((SessionFlags)flags, HOST) ||
             flags == SINGLEPLAYER_WITH_STATS) {
    // Write user contexts. After session creation these are read only!
    contexts_.insert(user_profile->contexts_.cbegin(),
                     user_profile->contexts_.cend());

    CreateHostSession(pSessionInfo, pNonce, user_index, public_slots,
                      private_slots, flags);
  } else {
    JoinExistingSession(pSessionInfo);
  }

  is_session_created_ = true;
  return X_ERROR_SUCCESS;
}

void XSession::GenerateExchangeKey(XNKEY* key) {
  for (int i = 0; i < sizeof(XNKEY); i++) {
    key->ab[i] = i;
  }
}

uint64_t XSession::GenerateSessionId() {
  std::random_device rd;
  std::uniform_int_distribution<uint64_t> dist(0, -1);
  return ((uint64_t)0xAE << 56) | (dist(rd) & 0x0000FFFFFFFFFFFF);
}

X_RESULT XSession::CreateHostSession(XSESSION_INFO* session_info,
                                     uint64_t* nonce_ptr, uint8_t user_index,
                                     uint8_t public_slots,
                                     uint8_t private_slots, uint32_t flags) {
  if (!cvars::upnp) {
    XELOGI("Hosting while UPnP is disabled!");
  }

  std::random_device rd;
  std::uniform_int_distribution<uint64_t> dist(0, -1);
  *nonce_ptr = dist(rd);

  XSessionData* session_data = new XSessionData();
  session_data->user_index = user_index;
  session_data->num_slots_public = public_slots;
  session_data->num_slots_private = private_slots;
  session_data->flags = flags;
  session_id_ = GenerateSessionId();
  Uint64toXNKID(session_id_, &session_info->sessionID);

  XLiveAPI::XSessionCreate(session_id_, session_data);

  XELOGI("Created session {:016X}", session_id_);

  XLiveAPI::SessionContextSet(session_id_, contexts_);

  session_info->hostAddress.inaOnline.s_addr =
      XLiveAPI::OnlineIP().sin_addr.s_addr;

  session_info->hostAddress.ina.s_addr =
      session_info->hostAddress.inaOnline.s_addr;

  memcpy(&session_info->hostAddress.abEnet, XLiveAPI::mac_address_->raw(), 6);
  memcpy(&session_info->hostAddress.abOnline, XLiveAPI::mac_address_->raw(), 6);
  session_info->hostAddress.wPortOnline = XLiveAPI::GetPlayerPort();
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::CreateStatsSession(XSESSION_INFO* session_info,
                                      uint64_t* nonce_ptr, uint8_t user_index,
                                      uint8_t public_slots,
                                      uint8_t private_slots, uint32_t flags) {
  return CreateHostSession(session_info, nonce_ptr, user_index, public_slots,
                           private_slots, flags);
}

X_RESULT XSession::JoinExistingSession(XSESSION_INFO* session_info) {
  session_id_ = XNKIDtoUint64(&session_info->sessionID);
  XELOGI("Joining session {:016X}", session_id_);

  assert_true(IsOnlineSession(session_id_));

  if (session_id_ == NULL) {
    assert_always();
    return X_E_FAIL;
  }

  const std::unique_ptr<SessionObjectJSON> session =
      XLiveAPI::XSessionGet(session_id_);

  // Begin XNetRegisterKey?
  session_info->hostAddress.inaOnline = ip_to_in_addr(session->HostAddress());

  session_info->hostAddress.ina.s_addr =
      session_info->hostAddress.inaOnline.s_addr;

  memcpy(&session_info->hostAddress.abEnet,
         MacAddress(session->MacAddress()).raw(), 6);
  memcpy(&session_info->hostAddress.abOnline,
         MacAddress(session->MacAddress()).raw(), 6);

  session_info->hostAddress.wPortOnline = XLiveAPI::GetPlayerPort();

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::DeleteSession() {
  // Begin XNetUnregisterKey?
  XLiveAPI::DeleteSession(session_id_);
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::JoinSession(XSessionJoin* data) {
  const bool join_local = data->xuid_array_ptr == 0;
  std::string join_type =
      join_local ? "XGISessionJoinLocal" : "XGISessionJoinRemote";
  XELOGI("{}({:08X}, {}, {:08X}, {:08X}, {:08X})", join_type, data->obj_ptr,
         data->array_count, data->xuid_array_ptr, data->indices_array_ptr,
         data->private_slots_array_ptr);

  std::vector<std::string> xuids{};

  const auto xuid_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuid_array_ptr);

  const auto indices_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          data->indices_array_ptr);

  const auto private_slots = kernel_state_->memory()->TranslateVirtual<bool*>(
      data->private_slots_array_ptr);

  for (uint32_t i = 0; i < data->array_count; i++) {
    if (join_local) {
      const uint32_t index = (uint32_t)indices_array[i];
      const auto profile = kernel_state()->user_profile(index);

      if (!profile) {
        assert_always();
        return X_E_FAIL;
      }

      const auto xuid = profile->xuid();

      // Convert local user index to xuid.
      xuids.push_back(string_util::to_hex_string(xuid));
    } else {
      xuids.push_back(string_util::to_hex_string(xuid_array[i]));
    }

    const bool private_slot = private_slots[i];

    if (private_slot) {
      XELOGI("Occupying private slot");
    } else {
      XELOGI("Occupying public slot");
    }
  }

  XLiveAPI::SessionJoinRemote(session_id_, xuids);
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::LeaveSession(XSessionLeave* data) {
  const bool leavelocal = data->xuid_array_ptr == 0;

  std::string leave_type =
      leavelocal ? "XGISessionLeaveLocal" : "XGISessionLeaveRemote";

  XELOGI("{}({:08X}, {}, {:08X}, {:08X})", leave_type, data->obj_ptr,
         data->array_count, data->xuid_array_ptr, data->indices_array_ptr);

  std::vector<std::string> xuids{};

  auto xuid_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuid_array_ptr);

  auto indices_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint32_t>*>(
          data->indices_array_ptr);

  for (uint32_t i = 0; i < data->array_count; i++) {
    if (leavelocal) {
      const uint32_t index = (uint32_t)indices_array[i];
      const auto profile = kernel_state()->user_profile(index);

      if (!profile) {
        assert_always();
        return X_E_FAIL;
      }

      // Convert local user index to xuid.
      xuids.push_back(string_util::to_hex_string(profile->xuid()));
    } else {
      xuids.push_back(string_util::to_hex_string(xuid_array[i]));
    }
  }

  XLiveAPI::SessionLeaveRemote(session_id_, xuids);
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::ModifySession(XSessionModify* data) {
  XELOGI("Modifying session {:016X}", session_id_);
  PrintSessionType(static_cast<SessionFlags>((uint32_t)data->flags));

  XLiveAPI::SessionModify(session_id_, data);
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::GetSessionDetails(XSessionDetails* data) {
  auto details =
      kernel_state_->memory()->TranslateVirtual<XSESSION_LOCAL_DETAILS*>(
          data->details_buffer);

  const std::unique_ptr<SessionObjectJSON> session =
      XLiveAPI::SessionDetails(session_id_);

  if (session->HostAddress().empty()) {
    return 1;
  }

  Uint64toXNKID(xe::byte_swap(session->SessionID_UInt()),
                &details->sessionInfo.sessionID);

  details->sessionInfo.hostAddress.inaOnline =
      ip_to_in_addr(session->HostAddress());

  details->sessionInfo.hostAddress.ina.s_addr =
      details->sessionInfo.hostAddress.inaOnline.s_addr;

  memcpy(&details->sessionInfo.hostAddress.abEnet,
         MacAddress(session->HostAddress()).raw(), 6);
  details->sessionInfo.hostAddress.wPortOnline = session->Port();

  details->UserIndexHost = 0;
  details->GameMode = 0;
  details->GameType = 0;
  details->Flags = session->Flags();
  details->MaxPublicSlots = session->PublicSlotsCount();
  details->MaxPrivateSlots = session->PrivateSlotsCount();

  // TODO:
  // Provide the correct counts.
  details->AvailablePrivateSlots = session->OpenPrivateSlotsCount();
  details->AvailablePublicSlots = session->OpenPublicSlotsCount();
  details->ActualMemberCount = session->FilledPublicSlotsCount();
  // details->ActualMemberCount =
  //     session->FilledPublicSlotsCount() + session->FilledPrivateSlotsCount();

  details->ReturnedMemberCount = (uint32_t)session->Players().size();
  details->eState = XSESSION_STATE::LOBBY;

  std::random_device rnd;
  std::mt19937_64 gen(rnd());
  std::uniform_int_distribution<uint64_t> dist(0, -1);

  details->Nonce = dist(rnd);

  for (int i = 0; i < sizeof(XNKEY); i++) {
    details->sessionInfo.keyExchangeKey.ab[i] = i;
  }

  for (int i = 0; i < sizeof(XNADDR::abOnline); i++) {
    details->sessionInfo.hostAddress.abOnline[i] = i;
  }

  uint32_t members_ptr = kernel_state_->memory()->SystemHeapAlloc(
      sizeof(XSESSION_MEMBER) * details->ReturnedMemberCount);

  auto members =
      kernel_state_->memory()->TranslateVirtual<XSESSION_MEMBER*>(members_ptr);
  details->pSessionMembers = members_ptr;

  for (uint8_t i = 0; i < details->ReturnedMemberCount; i++) {
    members[i].UserIndex = 0xFE;
    members[i].xuidOnline = session->Players().at(i).XUID();
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::MigrateHost(XSessionMigate* data) {
  auto sessionInfo = kernel_state_->memory()->TranslateVirtual<XSESSION_INFO*>(
      data->session_info_ptr);

  if (!XLiveAPI::upnp_handler->is_active()) {
    XELOGI("Migrating without UPnP");
    // return X_E_FAIL;
  }

  const auto result = XLiveAPI::XSessionMigration(session_id_);

  if (!result->SessionID_UInt()) {
    XELOGI("Session Migration Failed");
    return X_E_FAIL;
  }

  Uint64toXNKID(result->SessionID_UInt(), &sessionInfo->sessionID);

  sessionInfo->hostAddress.inaOnline.s_addr =
      XLiveAPI::OnlineIP().sin_addr.s_addr;

  sessionInfo->hostAddress.ina.s_addr =
      sessionInfo->hostAddress.inaOnline.s_addr;

  memcpy(&sessionInfo->hostAddress.abEnet, XLiveAPI::mac_address_->raw(), 6);
  memcpy(&sessionInfo->hostAddress.abOnline, XLiveAPI::mac_address_->raw(), 6);

  sessionInfo->hostAddress.wPortOnline = XLiveAPI::GetPlayerPort();

  for (int i = 0; i < sizeof(XNKEY); i++) {
    sessionInfo->keyExchangeKey.ab[i] = i;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::RegisterArbitration(XSessionArbitrationData* data) {
  XSESSION_REGISTRATION_RESULTS* results =
      kernel_state_->memory()->TranslateVirtual<XSESSION_REGISTRATION_RESULTS*>(
          data->results);

  const auto result = XLiveAPI::XSessionArbitration(session_id_);

  const uint32_t registrants_ptr = kernel_state_->memory()->SystemHeapAlloc(
      uint32_t(sizeof(XSESSION_REGISTRANT) * result->Machines().size()));

  results->registrants_count = uint32_t(result->Machines().size());
  results->registrants_ptr = registrants_ptr;

  XSESSION_REGISTRANT* registrants =
      kernel_state_->memory()->TranslateVirtual<XSESSION_REGISTRANT*>(
          registrants_ptr);

  for (uint8_t i = 0; i < result->Machines().size(); i++) {
    registrants[i].bTrustworthiness = 1;

    registrants[i].MachineID = result->Machines()[i].machine_id;
    registrants[i].bNumUsers = result->Machines()[i].player_count;

    const uint32_t users_ptr = kernel_state_->memory()->SystemHeapAlloc(
        sizeof(uint64_t) * registrants[i].bNumUsers);

    uint64_t* users_xuid_ptr =
        kernel_state_->memory()->TranslateVirtual<uint64_t*>(users_ptr);

    for (uint8_t j = 0; j < registrants[i].bNumUsers; j++) {
      users_xuid_ptr[j] = result->Machines()[i].xuids[j];
    }

    registrants[i].rgUsers = users_ptr;
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::ModifySkill(XSessionModifySkill* data) {
  const auto xuid_array =
      kernel_state_->memory()->TranslateVirtual<xe::be<uint64_t>*>(
          data->xuid_array_ptr);

  for (uint32_t i = 0; i < data->array_count; i++) {
    const auto xuid = xuid_array[i];

    XELOGI("ModifySkill XUID: {:016X}", xuid);
  }

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::WriteStats(XSessionWriteStats* data) {
  XSessionViewProperties* leaderboard =
      kernel_state_->memory()->TranslateVirtual<XSessionViewProperties*>(
          data->leaderboards_guest_address);

  XLiveAPI::SessionWriteStats(session_id_, data, leaderboard);

  return X_ERROR_SUCCESS;
}

X_RESULT XSession::StartSession(uint32_t flags) { return X_ERROR_SUCCESS; }

X_RESULT XSession::EndSession() { return X_ERROR_SUCCESS; }

X_RESULT XSession::GetSessions(Memory* memory, XSessionSearch* search_data) {
  if (!search_data->results_buffer_size) {
    search_data->results_buffer_size =
        sizeof(XSESSION_SEARCHRESULT) * search_data->num_results;
    return ERROR_INSUFFICIENT_BUFFER;
  }

  const auto sessions = XLiveAPI::SessionSearch(search_data);

  const size_t session_count =
      std::min((size_t)search_data->num_results, sessions.size());

  const uint32_t session_search_result_data_address =
      search_data->search_results_ptr + sizeof(XSESSION_SEARCHRESULT_HEADER);

  XSESSION_SEARCHRESULT* result =
      memory->TranslateVirtual<XSESSION_SEARCHRESULT*>(
          search_data->search_results_ptr +
          sizeof(XSESSION_SEARCHRESULT_HEADER));

  for (uint8_t i = 0; i < session_count; i++) {
    const auto context =
        XLiveAPI::SessionContextGet(sessions.at(i)->SessionID_UInt());

    FillSessionContext(memory, context, &result[i]);
    FillSessionProperties(search_data->num_props, search_data->props_ptr,
                          &result[i]);
    FillSessionSearchResult(sessions.at(i), &result[i]);
  }

  XSESSION_SEARCHRESULT_HEADER* resultsHeader =
      memory->TranslateVirtual<XSESSION_SEARCHRESULT_HEADER*>(
          search_data->search_results_ptr);

  resultsHeader->search_results_count = (uint32_t)session_count;
  resultsHeader->search_results_ptr = session_search_result_data_address;
  return X_ERROR_SUCCESS;
}

X_RESULT XSession::GetSessionByID(Memory* memory,
                                  XSessionSearchID* search_data) {
  if (!search_data->results_buffer_size) {
    search_data->results_buffer_size = sizeof(XSESSION_SEARCHRESULT);
    return ERROR_INSUFFICIENT_BUFFER;
  }

  const auto sessionId = XNKIDtoUint64(&search_data->session_id);

  if (!sessionId) {
    return ERROR_SUCCESS;
  }

  const auto session = XLiveAPI::XSessionGet(sessionId);

  if (session->HostAddress().empty()) {
    return ERROR_SUCCESS;
  }

  XSESSION_SEARCHRESULT* result =
      memory->TranslateVirtual<XSESSION_SEARCHRESULT*>(
          search_data->search_results_ptr +
          sizeof(XSESSION_SEARCHRESULT_HEADER));

  // HUH? How it should be filled in this case?
  FillSessionContext(memory, {}, result);
  FillSessionProperties(0, 0, result);
  FillSessionSearchResult(session, result);

  XSESSION_SEARCHRESULT_HEADER* resultsHeader =
      memory->TranslateVirtual<XSESSION_SEARCHRESULT_HEADER*>(
          search_data->search_results_ptr);

  resultsHeader->search_results_count = 1;
  resultsHeader->search_results_ptr =
      search_data->search_results_ptr + sizeof(XSESSION_SEARCHRESULT_HEADER);

  return X_ERROR_SUCCESS;
}

void XSession::FillSessionSearchResult(
    const std::unique_ptr<SessionObjectJSON>& session_info,
    XSESSION_SEARCHRESULT* result) {
  result->filled_priv_slots = session_info->FilledPrivateSlotsCount();
  result->filled_public_slots = session_info->FilledPublicSlotsCount();
  result->open_priv_slots = session_info->OpenPrivateSlotsCount();
  result->open_public_slots = session_info->OpenPublicSlotsCount();

  Uint64toXNKID(session_info->SessionID_UInt(), &result->info.sessionID);

  const MacAddress mac = MacAddress(session_info->MacAddress());

  memcpy(&result->info.hostAddress.abEnet, mac.raw(),
         sizeof(result->info.hostAddress.abEnet));
  memcpy(&result->info.hostAddress.abOnline, mac.raw(),
         sizeof(result->info.hostAddress.abEnet));

  for (int j = 0; j < sizeof(XNKEY); j++) {
    result->info.keyExchangeKey.ab[j] = j;
  }

  result->info.hostAddress.inaOnline =
      ip_to_in_addr(session_info->HostAddress());

  result->info.hostAddress.ina.s_addr =
      result->info.hostAddress.inaOnline.s_addr;

  result->info.hostAddress.wPortOnline = session_info->Port();
}

void XSession::FillSessionContext(Memory* memory,
                                  std::map<uint32_t, uint32_t> contexts,
                                  XSESSION_SEARCHRESULT* result) {
  result->contexts_count = (uint32_t)contexts.size();

  const uint32_t context_ptr = memory->SystemHeapAlloc(
      uint32_t(sizeof(XUSER_CONTEXT) * contexts.size()));

  XUSER_CONTEXT* contexts_to_get =
      memory->TranslateVirtual<XUSER_CONTEXT*>(context_ptr);

  uint32_t i = 0;
  for (const auto context : contexts) {
    contexts_to_get[i].context_id = context.first;
    contexts_to_get[i].value = context.second;
    i++;
  }

  result->contexts_ptr = context_ptr;
}

void XSession::FillSessionProperties(uint32_t properties_count,
                                     uint32_t properties_ptr,
                                     XSESSION_SEARCHRESULT* result) {
  result->properties_count = properties_count;
  result->properties_ptr = properties_ptr;
}

void XSession::PrintSessionType(SessionFlags flags) {
  std::string session_description = "";

  const std::map<SessionFlags, std::string> basic = {
      {HOST, "Host"},
      {PRESENCE, "Presence"},
      {STATS, "Stats"},
      {MATCHMAKING, "Matchmaking"},
      {ARBITRATION, "Arbitration"},
      {PEER_NETWORK, "Peer Network"},
      {SOCIAL_MATCHMAKING_ALLOWED, "Social Matchmaking"},
      {INVITES_DISABLED, "No invites"},
      {JOIN_VIA_PRESENCE_DISABLED, "Presence Join Disabled"},
      {JOIN_IN_PROGRESS_DISABLED, "In-Progress Join Disabled"},
      {JOIN_VIA_PRESENCE_FRIENDS_ONLY, "Friends Only"}};

  const std::map<SessionFlags, std::string> extended = {
      {SINGLEPLAYER_WITH_STATS, "Singleplayer with Stats"},
      {LIVE_MULTIPLAYER_STANDARD, "LIVE: Multiplayer"},
      {LIVE_MULTIPLAYER_RANKED, "LIVE: Multiplayer Ranked"},
      {GROUP_LOBBY, "Group Lobby"},
      {GROUP_GAME, "Group Game"}};

  for (const auto& entry : basic) {
    if (HasSessionFlag(flags, entry.first)) {
      session_description.append(entry.second + ", ");
    }
  }

  XELOGI("Session Description: {}", session_description);
  session_description.clear();

  for (const auto& entry : extended) {
    if (HasSessionFlag(flags, entry.first)) {
      session_description.append(entry.second + ", ");
    }
  }

  XELOGI("Session Extended Description: {}", session_description);
}

}  // namespace kernel
}  // namespace xe