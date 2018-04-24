/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/config_loader.h"

#include "mtproto/dc_options.h"
#include "mtproto/mtp_instance.h"
#include "mtproto/special_config_request.h"

namespace MTP {
namespace internal {
namespace {

constexpr auto kEnumerateDcTimeout = 8000; // 8 seconds timeout for help_getConfig to work (then move to other dc)
constexpr auto kSpecialRequestTimeoutMs = 6000; // 4 seconds timeout for it to work in a specially requested dc.

} // namespace

ConfigLoader::ConfigLoader(
	not_null<Instance*> instance,
	RPCDoneHandlerPtr onDone,
	RPCFailHandlerPtr onFail)
: _instance(instance)
, _doneHandler(onDone)
, _failHandler(onFail) {
	_enumDCTimer.setCallback([this] { enumerate(); });
	_specialEnumTimer.setCallback([this] { sendSpecialRequest(); });
}

void ConfigLoader::load() {
	if (!_instance->isKeysDestroyer()) {
		sendRequest(_instance->mainDcId());
		_enumDCTimer.callOnce(kEnumerateDcTimeout);
	} else {
		auto ids = _instance->dcOptions()->configEnumDcIds();
		Assert(!ids.empty());
		_enumCurrent = ids.front();
		enumerate();
	}
}

mtpRequestId ConfigLoader::sendRequest(ShiftedDcId shiftedDcId) {
	return _instance->send(
		MTPhelp_GetConfig(),
		base::duplicate(_doneHandler),
		base::duplicate(_failHandler),
		shiftedDcId);
}

DcId ConfigLoader::specialToRealDcId(DcId specialDcId) {
	return Instance::Config::kTemporaryMainDc + specialDcId;
}

void ConfigLoader::terminateRequest() {
	if (_enumRequest) {
		_instance->cancel(base::take(_enumRequest));
	}
	if (_enumCurrent) {
		_instance->killSession(MTP::configDcId(_enumCurrent));
	}
}

void ConfigLoader::terminateSpecialRequest() {
	if (_specialEnumRequest) {
		_instance->cancel(base::take(_specialEnumRequest));
	}
	if (_specialEnumCurrent) {
		_instance->killSession(_specialEnumCurrent);
	}
}

ConfigLoader::~ConfigLoader() {
	terminateRequest();
	terminateSpecialRequest();
}

void ConfigLoader::enumerate() {
	terminateRequest();
	if (!_enumCurrent) {
		_enumCurrent = _instance->mainDcId();
	}
	auto ids = _instance->dcOptions()->configEnumDcIds();
	Assert(!ids.empty());

	auto i = std::find(ids.cbegin(), ids.cend(), _enumCurrent);
	if (i == ids.cend() || (++i) == ids.cend()) {
		_enumCurrent = ids.front();
	} else {
		_enumCurrent = *i;
	}
	_enumRequest = sendRequest(MTP::configDcId(_enumCurrent));

	_enumDCTimer.callOnce(kEnumerateDcTimeout);

	createSpecialLoader();
}

void ConfigLoader::createSpecialLoader() {
	if (Global::ConnectionProxy().type != ProxyData::Type::None) {
		_specialLoader.reset();
		return;
	}
	if (!_specialLoader || (!_specialEnumRequest && _specialEndpoints.empty())) {
		_specialLoader = std::make_unique<SpecialConfigRequest>([this](DcId dcId, const std::string &ip, int port) {
			addSpecialEndpoint(dcId, ip, port);
		});
		_triedSpecialEndpoints.clear();
	}
}

void ConfigLoader::addSpecialEndpoint(DcId dcId, const std::string &ip, int port) {
	auto endpoint = SpecialEndpoint { dcId, ip, port };
	if (base::contains(_specialEndpoints, endpoint)
		|| base::contains(_triedSpecialEndpoints, endpoint)) {
		return;
	}
	DEBUG_LOG(("MTP Info: Special endpoint received, '%1:%2'").arg(ip.c_str()).arg(port));
	_specialEndpoints.push_back(endpoint);

	if (!_specialEnumTimer.isActive()) {
		_specialEnumTimer.callOnce(1);
	}
}

void ConfigLoader::sendSpecialRequest() {
	terminateSpecialRequest();
	if (Global::ConnectionType() != dbictAuto) {
		_specialLoader.reset();
		return;
	}
	if (_specialEndpoints.empty()) {
		createSpecialLoader();
		return;
	}

	const auto weak = base::make_weak(this);
	const auto index = rand_value<uint32>() % _specialEndpoints.size();
	const auto endpoint = _specialEndpoints.begin() + index;
	_specialEnumCurrent = specialToRealDcId(endpoint->dcId);
	_instance->dcOptions()->constructAddOne(
		_specialEnumCurrent,
		MTPDdcOption::Flag::f_tcpo_only,
		endpoint->ip,
		endpoint->port);
	_specialEnumRequest = _instance->send(
		MTPhelp_GetConfig(),
		rpcDone([weak](const MTPConfig &result) {
			if (const auto strong = weak.get()) {
				strong->specialConfigLoaded(result);
			}
		}),
		base::duplicate(_failHandler),
		_specialEnumCurrent);
	_triedSpecialEndpoints.push_back(*endpoint);
	_specialEndpoints.erase(endpoint);

	_specialEnumTimer.callOnce(kSpecialRequestTimeoutMs);
}

void ConfigLoader::specialConfigLoaded(const MTPConfig &result) {
	Expects(result.type() == mtpc_config);

	auto &data = result.c_config();
	if (data.vdc_options.v.empty()) {
		LOG(("MTP Error: config with empty dc_options received!"));
		return;
	}

	// We use special config only for dc options.
	// For everything else we wait for normal config from main dc.
	_instance->dcOptions()->setFromList(data.vdc_options);
}

} // namespace internal
} // namespace MTP
