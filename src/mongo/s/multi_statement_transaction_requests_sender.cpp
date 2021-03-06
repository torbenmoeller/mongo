/**
 *    Copyright (C) 2018 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#include "mongo/platform/basic.h"

#include "mongo/s/multi_statement_transaction_requests_sender.h"

#include "mongo/s/transaction/transaction_router.h"

namespace mongo {

namespace {

std::vector<AsyncRequestsSender::Request> attachTxnDetails(
    OperationContext* opCtx, const std::vector<AsyncRequestsSender::Request>& requests) {
    auto txnRouter = TransactionRouter::get(opCtx);
    if (!txnRouter) {
        return requests;
    }

    std::vector<AsyncRequestsSender::Request> newRequests;
    newRequests.reserve(requests.size());

    for (auto request : requests) {
        auto& participant = txnRouter->getOrCreateParticipant(request.shardId);
        newRequests.emplace_back(request.shardId,
                                 participant.attachTxnFieldsIfNeeded(request.cmdObj));
    }

    return newRequests;
}

}  // unnamed namespace

MultiStatementTransactionRequestsSender::MultiStatementTransactionRequestsSender(
    OperationContext* opCtx,
    executor::TaskExecutor* executor,
    StringData dbName,
    const std::vector<AsyncRequestsSender::Request>& requests,
    const ReadPreferenceSetting& readPreference,
    Shard::RetryPolicy retryPolicy)
    : _opCtx(opCtx),
      _ars(
          opCtx, executor, dbName, attachTxnDetails(opCtx, requests), readPreference, retryPolicy) {
}

bool MultiStatementTransactionRequestsSender::done() {
    return _ars.done();
}

AsyncRequestsSender::Response MultiStatementTransactionRequestsSender::next() {
    auto result = _ars.next();

    if (auto txnRouter = TransactionRouter::get(_opCtx)) {
        auto& participant = txnRouter->getOrCreateParticipant(result.shardId);
        participant.markAsCommandSent();
    }

    return result;
}

void MultiStatementTransactionRequestsSender::stopRetrying() {
    _ars.stopRetrying();
}

}  // namespace mongo
