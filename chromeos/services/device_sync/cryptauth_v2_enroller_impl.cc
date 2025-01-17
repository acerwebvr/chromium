// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/services/device_sync/cryptauth_v2_enroller_impl.h"

#include <utility>

#include "base/bind.h"
#include "base/memory/ptr_util.h"
#include "base/no_destructor.h"
#include "chromeos/components/multidevice/logging/logging.h"
#include "chromeos/services/device_sync/cryptauth_client.h"
#include "chromeos/services/device_sync/cryptauth_constants.h"
#include "chromeos/services/device_sync/cryptauth_key_creator_impl.h"
#include "chromeos/services/device_sync/cryptauth_key_proof_computer_impl.h"
#include "chromeos/services/device_sync/cryptauth_key_registry.h"
#include "chromeos/services/device_sync/proto/cryptauth_client_app_metadata.pb.h"
#include "chromeos/services/device_sync/proto/cryptauth_common.pb.h"
#include "chromeos/services/device_sync/public/cpp/gcm_constants.h"

namespace chromeos {

namespace device_sync {

namespace {

using cryptauthv2::SyncKeysRequest;
using SyncSingleKeyRequest = cryptauthv2::SyncKeysRequest::SyncSingleKeyRequest;

using cryptauthv2::SyncKeysResponse;
using SyncSingleKeyResponse =
    cryptauthv2::SyncKeysResponse::SyncSingleKeyResponse;
using KeyAction =
    cryptauthv2::SyncKeysResponse::SyncSingleKeyResponse::KeyAction;
using KeyCreation =
    cryptauthv2::SyncKeysResponse::SyncSingleKeyResponse::KeyCreation;

using cryptauthv2::EnrollKeysRequest;
using EnrollSingleKeyRequest =
    cryptauthv2::EnrollKeysRequest::EnrollSingleKeyRequest;

using cryptauthv2::EnrollKeysResponse;
using EnrollSingleKeyResponse =
    cryptauthv2::EnrollKeysResponse::EnrollSingleKeyResponse;

// Timeout values for asynchronous operations.
// TODO(https://crbug.com/933656): Tune these values.
constexpr base::TimeDelta kWaitingForSyncKeysResponseTimeout =
    base::TimeDelta::FromSeconds(10);
constexpr base::TimeDelta kWaitingForKeyCreationTimeout =
    base::TimeDelta::FromSeconds(10);
constexpr base::TimeDelta kWaitingForEnrollKeysResponseTimeout =
    base::TimeDelta::FromSeconds(10);

CryptAuthEnrollmentResult::ResultCode SyncKeysNetworkRequestErrorToResultCode(
    NetworkRequestError error) {
  switch (error) {
    case NetworkRequestError::kOffline:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorSyncKeysApiCallOffline;
    case NetworkRequestError::kEndpointNotFound:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorSyncKeysApiCallEndpointNotFound;
    case NetworkRequestError::kAuthenticationError:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorSyncKeysApiCallAuthenticationError;
    case NetworkRequestError::kBadRequest:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorSyncKeysApiCallBadRequest;
    case NetworkRequestError::kResponseMalformed:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorSyncKeysApiCallResponseMalformed;
    case NetworkRequestError::kInternalServerError:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorSyncKeysApiCallInternalServerError;
    case NetworkRequestError::kUnknown:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorSyncKeysApiCallUnknownError;
  }
}

CryptAuthEnrollmentResult::ResultCode EnrollKeysNetworkRequestErrorToResultCode(
    NetworkRequestError error) {
  switch (error) {
    case NetworkRequestError::kOffline:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorEnrollKeysApiCallOffline;
    case NetworkRequestError::kEndpointNotFound:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorEnrollKeysApiCallEndpointNotFound;
    case NetworkRequestError::kAuthenticationError:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorEnrollKeysApiCallAuthenticationError;
    case NetworkRequestError::kBadRequest:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorEnrollKeysApiCallBadRequest;
    case NetworkRequestError::kResponseMalformed:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorEnrollKeysApiCallResponseMalformed;
    case NetworkRequestError::kInternalServerError:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorEnrollKeysApiCallInternalServerError;
    case NetworkRequestError::kUnknown:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorEnrollKeysApiCallUnknownError;
  }
}

bool DoesDeviceSoftwarePackageWithExpectedNameExist(
    const google::protobuf::RepeatedPtrField<
        cryptauthv2::ApplicationSpecificMetadata>& app_specific_metadata_list,
    const std::string& expected_name) {
  for (const cryptauthv2::ApplicationSpecificMetadata& metadata :
       app_specific_metadata_list) {
    if (metadata.device_software_package() == expected_name)
      return true;
  }
  return false;
}

// The v2 Enrollment protocol states that the order of the received
// SyncSingleKeyResponses will correspond to the order of the
// SyncSingleKeyRequests. That order is defined here.
const std::vector<CryptAuthKeyBundle::Name>& GetKeyBundleOrder() {
  static const base::NoDestructor<std::vector<CryptAuthKeyBundle::Name>> order(
      [] {
        std::vector<CryptAuthKeyBundle::Name> order;
        for (const CryptAuthKeyBundle::Name& bundle_name :
             CryptAuthKeyBundle::AllNames()) {
          order.push_back(bundle_name);
        }
        return order;
      }());

  return *order;
}

CryptAuthKey::Status ConvertKeyCreationToKeyStatus(KeyCreation key_creation) {
  switch (key_creation) {
    case SyncSingleKeyResponse::ACTIVE:
      return CryptAuthKey::Status::kActive;
    case SyncSingleKeyResponse::INACTIVE:
      return CryptAuthKey::Status::kInactive;
    default:
      NOTREACHED();
      return CryptAuthKey::Status::kInactive;
  }
}

// Return an error code if the SyncKeysResponse is invalid and null otherwise.
base::Optional<CryptAuthEnrollmentResult::ResultCode> CheckSyncKeysResponse(
    const SyncKeysResponse& response,
    size_t expected_num_key_responses) {
  if (response.random_session_id().empty()) {
    PA_LOG(ERROR) << "Missing SyncKeysResponse::random_session_id.";
    return CryptAuthEnrollmentResult::ResultCode::
        kErrorSyncKeysResponseMissingRandomSessionId;
  }

  if (!response.has_client_directive() ||
      response.client_directive().checkin_delay_millis() <= 0 ||
      response.client_directive().retry_attempts() < 0 ||
      response.client_directive().retry_period_millis() <= 0) {
    PA_LOG(ERROR) << "Invalid SyncKeysResponse::client_directive.";
    return CryptAuthEnrollmentResult::ResultCode::
        kErrorSyncKeysResponseInvalidClientDirective;
  }

  size_t num_single_responses =
      static_cast<size_t>(response.sync_single_key_responses_size());
  if (num_single_responses != expected_num_key_responses) {
    PA_LOG(ERROR) << "Expected " << expected_num_key_responses << " "
                  << "SyncKeysResponse::sync_single_key_responses but "
                  << "received " << num_single_responses << ".";
    return CryptAuthEnrollmentResult::ResultCode::
        kErrorWrongNumberOfSyncSingleKeyResponses;
  }

  return base::nullopt;
}

// Given the key actions for the existing keys in the bundle, find the key to
// activate and the keys to delete, setting |handle_to_activate| and
// |handles_to_delete|, respectively. Returns an error code if the key actions
// are invalid and null otherwise.
//
// Note: The v2 Enrollment protocol states, "If the client has at least one
// enrolled key, there must be exactly one ACTIVATE key action (unless the
// server wants to delete all keys currently held by the client). This is
// because there must be exactly one 'active' key after processing these
// actions."
base::Optional<CryptAuthEnrollmentResult::ResultCode> ProcessKeyActions(
    const google::protobuf::RepeatedField<int>& key_actions,
    const std::vector<std::string>& handle_order,
    base::Optional<std::string>* handle_to_activate,
    std::vector<std::string>* handles_to_delete) {
  // Check that the number of key actions agrees with the number of key
  // handles sent in the SyncSingleKeysRequest.
  if (static_cast<size_t>(key_actions.size()) != handle_order.size()) {
    PA_LOG(ERROR) << "Key bundle has " << handle_order.size() << " keys but "
                  << "SyncSingleKeyResponse::key_actions has size "
                  << key_actions.size();
    return CryptAuthEnrollmentResult::ResultCode::kErrorWrongNumberOfKeyActions;
  }

  // Find all keys that CryptAuth requests be deleted, and find the handle
  // of the key that will be active, if any. Note: The order of the key
  // actions is assumed to agree with the order of the key handles sent in
  // the SyncSingleKeyRequest.
  for (size_t i = 0; i < handle_order.size(); ++i) {
    if (!SyncSingleKeyResponse::KeyAction_IsValid(key_actions[i])) {
      PA_LOG(ERROR) << "Invalid KeyAction enum value " << key_actions[i];
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorInvalidKeyActionEnumValue;
    }

    KeyAction key_action = static_cast<KeyAction>(key_actions[i]);

    if (key_action == SyncSingleKeyResponse::DELETE) {
      handles_to_delete->emplace_back(handle_order[i]);
      continue;
    }

    if (key_action == SyncSingleKeyResponse::ACTIVATE) {
      // There cannot be more than one active handle.
      if (handle_to_activate->has_value()) {
        PA_LOG(ERROR) << "KeyActions specify two active handles: "
                      << **handle_to_activate << " and " << handle_order[i];
        return CryptAuthEnrollmentResult::ResultCode::
            kErrorKeyActionsSpecifyMultipleActiveKeys;
      }

      *handle_to_activate = handle_order[i];
    }
  }

  // The v2 Enrollment protocol states that, unless the server wants to
  // delete all keys currently held by the client, there should be exactly
  // one active key in the key bundle.
  if (!handle_to_activate->has_value() &&
      handles_to_delete->size() != handle_order.size()) {
    PA_LOG(ERROR) << "KeyActions do not specify an active handle.";
    return CryptAuthEnrollmentResult::ResultCode::
        kErrorKeyActionsDoNotSpecifyAnActiveKey;
  }

  return base::nullopt;
}

bool IsSupportedKeyType(const cryptauthv2::KeyType& key_type) {
  return key_type == cryptauthv2::KeyType::RAW128 ||
         key_type == cryptauthv2::KeyType::RAW256 ||
         key_type == cryptauthv2::KeyType::P256;
}

// Returns an error code if the key-creation instructions are invalid and null
// otherwise.
base::Optional<CryptAuthEnrollmentResult::ResultCode>
ProcessKeyCreationInstructions(
    const CryptAuthKeyBundle::Name& bundle_name,
    const SyncSingleKeyResponse& single_key_response,
    const std::string& server_ephemeral_dh,
    base::Optional<CryptAuthKeyCreator::CreateKeyData>* new_key_to_create,
    base::Optional<cryptauthv2::KeyDirective>* new_key_directive) {
  if (single_key_response.key_creation() == SyncSingleKeyResponse::NONE)
    return base::nullopt;

  if (!IsSupportedKeyType(single_key_response.key_type())) {
    PA_LOG(ERROR) << "KeyType " << single_key_response.key_type() << " "
                  << "not supported.";
    return CryptAuthEnrollmentResult::ResultCode::
        kErrorKeyCreationKeyTypeNotSupported;
  }

  // Symmetric keys cannot be created without the server's Diffie-Hellman key.
  if (server_ephemeral_dh.empty() &&
      (single_key_response.key_type() == cryptauthv2::KeyType::RAW128 ||
       single_key_response.key_type() == cryptauthv2::KeyType::RAW256)) {
    PA_LOG(ERROR)
        << "Missing server's Diffie-Hellman key. Cannot create symmetric keys.";
    return CryptAuthEnrollmentResult::ResultCode::
        kErrorSymmetricKeyCreationMissingServerDiffieHellman;
  }

  // CryptAuth demands that the key in the kUserKeyPair bundle has a fixed
  // handle name. For other key bundles, do not specify a handle name; let
  // CryptAuthKey generate a handle for us.
  base::Optional<std::string> new_key_handle;
  if (bundle_name == CryptAuthKeyBundle::Name::kUserKeyPair)
    new_key_handle = kCryptAuthFixedUserKeyPairHandle;

  *new_key_to_create = CryptAuthKeyCreator::CreateKeyData(
      ConvertKeyCreationToKeyStatus(single_key_response.key_creation()),
      single_key_response.key_type(), new_key_handle);

  if (single_key_response.has_key_directive())
    *new_key_directive = single_key_response.key_directive();

  return base::nullopt;
}

}  // namespace

// static
CryptAuthV2EnrollerImpl::Factory*
    CryptAuthV2EnrollerImpl::Factory::test_factory_ = nullptr;

// static
CryptAuthV2EnrollerImpl::Factory* CryptAuthV2EnrollerImpl::Factory::Get() {
  if (test_factory_)
    return test_factory_;

  static base::NoDestructor<CryptAuthV2EnrollerImpl::Factory> factory;
  return factory.get();
}

// static
void CryptAuthV2EnrollerImpl::Factory::SetFactoryForTesting(
    Factory* test_factory) {
  test_factory_ = test_factory;
}

CryptAuthV2EnrollerImpl::Factory::~Factory() = default;

std::unique_ptr<CryptAuthV2Enroller>
CryptAuthV2EnrollerImpl::Factory::BuildInstance(
    CryptAuthKeyRegistry* key_registry,
    CryptAuthClientFactory* client_factory,
    std::unique_ptr<base::OneShotTimer> timer) {
  return base::WrapUnique(new CryptAuthV2EnrollerImpl(
      key_registry, client_factory, std::move(timer)));
}

CryptAuthV2EnrollerImpl::CryptAuthV2EnrollerImpl(
    CryptAuthKeyRegistry* key_registry,
    CryptAuthClientFactory* client_factory,
    std::unique_ptr<base::OneShotTimer> timer)
    : key_registry_(key_registry),
      client_factory_(client_factory),
      timer_(std::move(timer)) {
  DCHECK(client_factory);
}

CryptAuthV2EnrollerImpl::~CryptAuthV2EnrollerImpl() = default;

// static
base::Optional<base::TimeDelta> CryptAuthV2EnrollerImpl::GetTimeoutForState(
    State state) {
  switch (state) {
    case State::kWaitingForSyncKeysResponse:
      return kWaitingForSyncKeysResponseTimeout;
    case State::kWaitingForKeyCreation:
      return kWaitingForKeyCreationTimeout;
    case State::kWaitingForEnrollKeysResponse:
      return kWaitingForEnrollKeysResponseTimeout;
    default:
      // Signifies that there should not be a timeout.
      return base::nullopt;
  }
}

// static
base::Optional<CryptAuthEnrollmentResult::ResultCode>
CryptAuthV2EnrollerImpl::ResultCodeErrorFromState(State state) {
  switch (state) {
    case State::kWaitingForSyncKeysResponse:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorTimeoutWaitingForSyncKeysResponse;
    case State::kWaitingForKeyCreation:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorTimeoutWaitingForKeyCreation;
    case State::kWaitingForEnrollKeysResponse:
      return CryptAuthEnrollmentResult::ResultCode::
          kErrorTimeoutWaitingForEnrollKeysResponse;
    default:
      return base::nullopt;
  }
}

void CryptAuthV2EnrollerImpl::OnAttemptStarted(
    const cryptauthv2::ClientMetadata& client_metadata,
    const cryptauthv2::ClientAppMetadata& client_app_metadata,
    const base::Optional<cryptauthv2::PolicyReference>&
        client_directive_policy_reference) {
  DCHECK(state_ == State::kNotStarted);

  SetState(State::kWaitingForSyncKeysResponse);

  cryptauth_client_ = client_factory_->CreateInstance();
  cryptauth_client_->SyncKeys(
      BuildSyncKeysRequest(client_metadata, client_app_metadata,
                           client_directive_policy_reference),
      base::Bind(&CryptAuthV2EnrollerImpl::OnSyncKeysSuccess,
                 base::Unretained(this)),
      base::Bind(&CryptAuthV2EnrollerImpl::OnSyncKeysFailure,
                 base::Unretained(this)));
}

void CryptAuthV2EnrollerImpl::SetState(State state) {
  timer_->Stop();

  PA_LOG(INFO) << "Transitioning from " << state_ << " to " << state;
  state_ = state;

  base::Optional<base::TimeDelta> timeout_for_state = GetTimeoutForState(state);
  if (!timeout_for_state)
    return;

  base::Optional<CryptAuthEnrollmentResult::ResultCode> error_code =
      ResultCodeErrorFromState(state);

  // If there's a timeout specified, there should be a corresponding error code.
  DCHECK(error_code);

  // TODO(https://crbug.com/936273): Add metrics to track failure rates due to
  // async timeouts.
  timer_->Start(FROM_HERE, *timeout_for_state,
                base::BindOnce(&CryptAuthV2EnrollerImpl::FinishAttempt,
                               base::Unretained(this), *error_code));
}

SyncKeysRequest CryptAuthV2EnrollerImpl::BuildSyncKeysRequest(
    const cryptauthv2::ClientMetadata& client_metadata,
    const cryptauthv2::ClientAppMetadata& client_app_metadata,
    const base::Optional<cryptauthv2::PolicyReference>&
        client_directive_policy_reference) {
  SyncKeysRequest request;
  request.set_application_name(kCryptAuthGcmAppId);

  // ApplicationSpecificMetadata::device_software_package must agree with
  // the SyncKeysRequest::application_name.
  DCHECK(DoesDeviceSoftwarePackageWithExpectedNameExist(
      client_app_metadata.application_specific_metadata(),
      request.application_name()));

  request.set_client_version(kCryptAuthClientVersion);
  request.mutable_client_metadata()->CopyFrom(client_metadata);
  request.set_client_app_metadata(client_app_metadata.SerializeAsString());

  if (client_directive_policy_reference) {
    request.mutable_policy_reference()->CopyFrom(
        *client_directive_policy_reference);
  }

  // Note: The v2 Enrollment protocol states that the order of the received
  // SyncSingleKeyResponses will correspond to the order of the
  // SyncSingleKeyRequests.
  for (const CryptAuthKeyBundle::Name& bundle_name : GetKeyBundleOrder()) {
    request.add_sync_single_key_requests()->CopyFrom(BuildSyncSingleKeyRequest(
        bundle_name, key_registry_->GetKeyBundle(bundle_name)));
  }

  return request;
}

SyncSingleKeyRequest CryptAuthV2EnrollerImpl::BuildSyncSingleKeyRequest(
    const CryptAuthKeyBundle::Name& bundle_name,
    const CryptAuthKeyBundle* key_bundle) {
  SyncSingleKeyRequest request;
  request.set_key_name(
      CryptAuthKeyBundle::KeyBundleNameEnumToString(bundle_name));
  // Note: Use of operator[] here adds an entry to the map if no entry currently
  // exists for |bundle_name|. If keys exist in the bundle, the empty handle
  // list will be populated below.
  std::vector<std::string>& handle_order = key_handle_orders_[bundle_name];

  if (!key_bundle)
    return request;

  // Note: The order of key_actions sent in the SyncSingleKeyResponse will
  // align with the order of the handles used here, which we store in
  // |key_handle_orders_|.
  for (const std::pair<std::string, CryptAuthKey>& handle_key_pair :
       key_bundle->handle_to_key_map()) {
    request.add_key_handles(handle_key_pair.first);

    handle_order.emplace_back(handle_key_pair.first);
  }

  if (key_bundle->key_directive() &&
      key_bundle->key_directive()->has_policy_reference()) {
    request.mutable_policy_reference()->CopyFrom(
        key_bundle->key_directive()->policy_reference());
  }

  return request;
}

void CryptAuthV2EnrollerImpl::OnSyncKeysSuccess(
    const SyncKeysResponse& response) {
  DCHECK(state_ == State::kWaitingForSyncKeysResponse);

  if (response.server_status() == SyncKeysResponse::SERVER_OVERLOADED) {
    FinishAttempt(
        CryptAuthEnrollmentResult::ResultCode::kErrorCryptAuthServerOverloaded);
    return;
  }

  base::Optional<CryptAuthEnrollmentResult::ResultCode> error_code =
      CheckSyncKeysResponse(response, GetKeyBundleOrder().size());
  if (error_code) {
    FinishAttempt(*error_code);
    return;
  }

  new_client_directive_ = response.client_directive();

  // Note: The server's Diffie-Hellman public key is only required if symmetric
  // keys need to be created.
  base::Optional<CryptAuthKey> server_ephemeral_dh;
  if (!response.server_ephemeral_dh().empty()) {
    server_ephemeral_dh = CryptAuthKey(
        response.server_ephemeral_dh(), std::string() /* private_key */,
        CryptAuthKey::Status::kInactive, cryptauthv2::KeyType::P256);
  }

  base::flat_map<CryptAuthKeyBundle::Name, CryptAuthKeyCreator::CreateKeyData>
      new_keys_to_create;
  base::flat_map<CryptAuthKeyBundle::Name, cryptauthv2::KeyDirective>
      new_key_directives;
  error_code = ProcessSingleKeyResponses(response, &new_keys_to_create,
                                         &new_key_directives);
  if (error_code) {
    FinishAttempt(*error_code);
    return;
  }

  // If CryptAuth did not request any new keys, the enrollment flow ends here.
  if (new_keys_to_create.empty()) {
    FinishAttempt(
        CryptAuthEnrollmentResult::ResultCode::kSuccessNoNewKeysNeeded);
    return;
  }

  SetState(State::kWaitingForKeyCreation);

  key_creator_ = CryptAuthKeyCreatorImpl::Factory::Get()->BuildInstance();
  key_creator_->CreateKeys(
      new_keys_to_create, server_ephemeral_dh,
      base::BindOnce(&CryptAuthV2EnrollerImpl::OnKeysCreated,
                     base::Unretained(this), response.random_session_id(),
                     new_key_directives));
}

base::Optional<CryptAuthEnrollmentResult::ResultCode>
CryptAuthV2EnrollerImpl::ProcessSingleKeyResponses(
    const SyncKeysResponse& sync_keys_response,
    base::flat_map<CryptAuthKeyBundle::Name,
                   CryptAuthKeyCreator::CreateKeyData>* new_keys_to_create,
    base::flat_map<CryptAuthKeyBundle::Name, cryptauthv2::KeyDirective>*
        new_key_directives) {
  // Starts as null but is overwritten with the ResultCode of the first error,
  // if any errors occur. If an error occurs for a single key bundle, proceed to
  // the next key bundle instead of exiting immediately.
  base::Optional<CryptAuthEnrollmentResult::ResultCode> error_code;

  for (size_t i = 0; i < GetKeyBundleOrder().size(); ++i) {
    // Note: The SyncSingleKeyRequests were ordered according to
    // GetKeyBundleOrder(), and the v2 Enrollment protocol specifies that the
    // SyncSingleKeyResponses will obey the same ordering as the requests.
    const SyncSingleKeyResponse& single_response =
        sync_keys_response.sync_single_key_responses(i);
    CryptAuthKeyBundle::Name bundle_name = GetKeyBundleOrder()[i];

    // Apply the key actions.
    // Important Note: The CryptAuth v2 Enrollment specification states, "the
    // key actions ACTIVATE, DEACTIVATE and DELETE should take effect right
    // after the client receives SyncKeysResponse. These actions should not
    // wait for the end of the session, such as receiving a successful
    // EnrollKeysResponse."
    base::Optional<std::string> handle_to_activate;
    std::vector<std::string> handles_to_delete;
    base::Optional<CryptAuthEnrollmentResult::ResultCode> error_code_actions =
        ProcessKeyActions(single_response.key_actions(),
                          key_handle_orders_[bundle_name], &handle_to_activate,
                          &handles_to_delete);

    // Do not apply the key actions or process the key creation instructions
    // if the key actions are invalid. Proceed to the next key bundle.
    if (error_code_actions) {
      // Set final error code if it hasn't already been set.
      if (!error_code)
        error_code = error_code_actions;

      continue;
    }

    for (const std::string& handle : handles_to_delete)
      key_registry_->DeleteKey(bundle_name, handle);

    if (handle_to_activate)
      key_registry_->SetActiveKey(bundle_name, *handle_to_activate);

    // Process new-key data, if any.
    base::Optional<CryptAuthKeyCreator::CreateKeyData> new_key_to_create;
    base::Optional<cryptauthv2::KeyDirective> new_key_directive;
    base::Optional<CryptAuthEnrollmentResult::ResultCode> error_code_creation =
        ProcessKeyCreationInstructions(bundle_name, single_response,
                                       sync_keys_response.server_ephemeral_dh(),
                                       &new_key_to_create, &new_key_directive);

    // If the key-creation instructions are invalid, do not add to the list of
    // keys to be created. Proceed to the next key bundle.
    if (error_code_creation) {
      // Set final error code if it hasn't already been set.
      if (!error_code)
        error_code = error_code_creation;

      continue;
    }

    if (new_key_to_create)
      new_keys_to_create->insert_or_assign(bundle_name, *new_key_to_create);

    if (new_key_directive)
      new_key_directives->insert_or_assign(bundle_name, *new_key_directive);
  }

  return error_code;
}

void CryptAuthV2EnrollerImpl::OnSyncKeysFailure(NetworkRequestError error) {
  FinishAttempt(SyncKeysNetworkRequestErrorToResultCode(error));
}

void CryptAuthV2EnrollerImpl::OnKeysCreated(
    const std::string& session_id,
    const base::flat_map<CryptAuthKeyBundle::Name, cryptauthv2::KeyDirective>&
        new_key_directives,
    const base::flat_map<CryptAuthKeyBundle::Name, CryptAuthKey>& new_keys,
    const base::Optional<CryptAuthKey>& client_ephemeral_dh) {
  DCHECK(state_ == State::kWaitingForKeyCreation);

  EnrollKeysRequest request;
  request.set_random_session_id(session_id);
  if (client_ephemeral_dh)
    request.set_client_ephemeral_dh(client_ephemeral_dh->public_key());

  std::unique_ptr<CryptAuthKeyProofComputer> key_proof_computer =
      CryptAuthKeyProofComputerImpl::Factory::Get()->BuildInstance();

  for (const std::pair<CryptAuthKeyBundle::Name, CryptAuthKey>& name_key_pair :
       new_keys) {
    const CryptAuthKeyBundle::Name& bundle_name = name_key_pair.first;
    const CryptAuthKey& new_key = name_key_pair.second;

    EnrollSingleKeyRequest* single_key_request =
        request.add_enroll_single_key_requests();
    single_key_request->set_key_name(
        CryptAuthKeyBundle::KeyBundleNameEnumToString(bundle_name));
    single_key_request->set_new_key_handle(new_key.handle());
    if (new_key.IsAsymmetricKey())
      single_key_request->set_key_material(new_key.public_key());

    // Compute key proofs for the new keys using the random_session_id from the
    // SyncKeysResponse as the payload and the particular salt specified by the
    // v2 Enrollment protocol.
    base::Optional<std::string> key_proof = key_proof_computer->ComputeKeyProof(
        new_key, session_id, kCryptAuthKeyProofSalt);
    if (!key_proof || key_proof->empty()) {
      FinishAttempt(CryptAuthEnrollmentResult::ResultCode::
                        kErrorKeyProofComputationFailed);
      return;
    }

    single_key_request->set_key_proof(*key_proof);
  }

  SetState(State::kWaitingForEnrollKeysResponse);

  cryptauth_client_ = client_factory_->CreateInstance();
  cryptauth_client_->EnrollKeys(
      request,
      base::Bind(&CryptAuthV2EnrollerImpl::OnEnrollKeysSuccess,
                 base::Unretained(this), new_key_directives, new_keys),
      base::Bind(&CryptAuthV2EnrollerImpl::OnEnrollKeysFailure,
                 base::Unretained(this)));
}

void CryptAuthV2EnrollerImpl::OnEnrollKeysSuccess(
    const base::flat_map<CryptAuthKeyBundle::Name, cryptauthv2::KeyDirective>&
        new_key_directives,
    const base::flat_map<CryptAuthKeyBundle::Name, CryptAuthKey>& new_keys,
    const EnrollKeysResponse& response) {
  DCHECK(state_ == State::kWaitingForEnrollKeysResponse);

  for (const std::pair<CryptAuthKeyBundle::Name, CryptAuthKey>& new_key :
       new_keys) {
    key_registry_->AddEnrolledKey(new_key.first, new_key.second);
  }

  for (const std::pair<CryptAuthKeyBundle::Name, cryptauthv2::KeyDirective>&
           new_key_directive : new_key_directives) {
    key_registry_->SetKeyDirective(new_key_directive.first,
                                   new_key_directive.second);
  }

  FinishAttempt(CryptAuthEnrollmentResult::ResultCode::kSuccessNewKeysEnrolled);
}

void CryptAuthV2EnrollerImpl::OnEnrollKeysFailure(NetworkRequestError error) {
  FinishAttempt(EnrollKeysNetworkRequestErrorToResultCode(error));
}

void CryptAuthV2EnrollerImpl::FinishAttempt(
    CryptAuthEnrollmentResult::ResultCode result_code) {
  SetState(State::kFinished);

  OnAttemptFinished(
      CryptAuthEnrollmentResult(result_code, new_client_directive_));
}

std::ostream& operator<<(std::ostream& stream,
                         const CryptAuthV2EnrollerImpl::State& state) {
  switch (state) {
    case CryptAuthV2EnrollerImpl::State::kNotStarted:
      stream << "[Enroller state: Not started]";
      break;
    case CryptAuthV2EnrollerImpl::State::kWaitingForSyncKeysResponse:
      stream << "[Enroller state: Waiting for SyncKeys response]";
      break;
    case CryptAuthV2EnrollerImpl::State::kWaitingForKeyCreation:
      stream << "[Enroller state: Waiting for key creation]";
      break;
    case CryptAuthV2EnrollerImpl::State::kWaitingForEnrollKeysResponse:
      stream << "[Enroller state: Waiting for EnrollKeys response]";
      break;
    case CryptAuthV2EnrollerImpl::State::kFinished:
      stream << "[Enroller state: Finished]";
      break;
  }

  return stream;
}

}  // namespace device_sync

}  // namespace chromeos
