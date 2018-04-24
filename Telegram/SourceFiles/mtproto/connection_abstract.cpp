/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "mtproto/connection_abstract.h"

#include "mtproto/connection_tcp.h"
#include "mtproto/connection_http.h"
#include "mtproto/connection_auto.h"
#include "mtproto/session.h"

namespace MTP {
namespace internal {

AbstractConnection::~AbstractConnection() {
}

mtpBuffer AbstractConnection::preparePQFake(const MTPint128 &nonce) {
	MTPReq_pq req_pq(nonce);
	mtpBuffer buffer;
	uint32 requestSize = req_pq.innerLength() >> 2;

	buffer.resize(0);
	buffer.reserve(8 + requestSize);
	buffer.push_back(0); // tcp packet len
	buffer.push_back(0); // tcp packet num
	buffer.push_back(0);
	buffer.push_back(0);
	buffer.push_back(0);
	buffer.push_back(unixtime());
	buffer.push_back(requestSize * 4);
	req_pq.write(buffer);
	buffer.push_back(0); // tcp crc32 hash

	return buffer;
}

MTPResPQ AbstractConnection::readPQFakeReply(const mtpBuffer &buffer) {
	const mtpPrime *answer(buffer.constData());
	uint32 len = buffer.size();
	if (len < 5) {
		LOG(("Fake PQ Error: bad request answer, len = %1").arg(len * sizeof(mtpPrime)));
		DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		throw Exception("bad pq reply");
	}
	if (answer[0] != 0 || answer[1] != 0 || (((uint32)answer[2]) & 0x03) != 1/* || (unixtime() - answer[3] > 300) || (answer[3] - unixtime() > 60)*/) { // didnt sync time yet
		LOG(("Fake PQ Error: bad request answer start (%1 %2 %3)").arg(answer[0]).arg(answer[1]).arg(answer[2]));
		DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		throw Exception("bad pq reply");
	}
	uint32 answerLen = (uint32)answer[4];
	if (answerLen != (len - 5) * sizeof(mtpPrime)) {
		LOG(("Fake PQ Error: bad request answer %1 <> %2").arg(answerLen).arg((len - 5) * sizeof(mtpPrime)));
		DEBUG_LOG(("Fake PQ Error: answer bytes %1").arg(Logs::mb(answer, len * sizeof(mtpPrime)).str()));
		throw Exception("bad pq reply");
	}
	const mtpPrime *from(answer + 5), *end(from + len - 5);
	MTPResPQ response;
	response.read(from, end);
	return response;
}

AbstractConnection *AbstractConnection::create(
		const ConnectionOptions &options,
		ShiftedDcId shiftedDcId,
		DcType type,
		QThread *thread) {
	const auto protocolDcId = (type == DcType::MediaDownload)
		? -MTP::bareDcId(shiftedDcId)
		: MTP::bareDcId(shiftedDcId);
	if ((type == DcType::Temporary) || (!options.useHttp)) {
		return new TCPConnection(thread, protocolDcId);
	} else if (!options.useTcp) {
		return new HTTPConnection(thread);
	}
	return new AutoConnection(thread, protocolDcId);
}

} // namespace internal
} // namespace MTP
