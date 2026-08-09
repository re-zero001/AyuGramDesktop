/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "api/api_messages_search_merged.h"

#include "history/history.h"

namespace Api {

MessagesSearchMerged::MessagesSearchMerged(not_null<History*> history)
: _apiSearch(history) {
	if (const auto migrated = history->migrateFrom()) {
		_migratedSearch.emplace(migrated);
	}

	_apiSearch.messagesFounds(
	) | rpl::on_next([=](const FoundMessages &data) {
		if (!_apiStarted || data.nextToken != _concatedFound.nextToken) {
			_apiStarted = true;
			_apiFinished = data.finished;
			_apiFailed = data.failed;
			_concatedFound = data;
			if (_migratedStarted) {
				_concatedFound.total = (_concatedFound.total >= 0
					&& _migratedFirstFound.total >= 0)
					? _concatedFound.total + _migratedFirstFound.total
					: -1;
			}
			if (_apiFinished) {
				addMigratedFirstFound();
			}
			_concatedFound.failed = _apiFailed
				|| (_migratedStarted && _migratedFailed);
			_concatedFound.finished = _apiFinished
				&& (!_migratedSearch || (_migratedStarted && _migratedFinished))
				&& !_concatedFound.failed;
			_newFounds.fire({});
		} else {
			addFound(data);
			_apiFinished = data.finished;
			_apiFailed = data.failed;
			_concatedFound.total = (_concatedFound.total >= 0
				&& data.total >= 0)
				? data.total + (_migratedStarted
					&& _migratedFirstFound.total >= 0
					? _migratedFirstFound.total
					: 0)
				: -1;
			if (_apiFinished) {
				addMigratedFirstFound();
			}
			_concatedFound.failed = _apiFailed
				|| (_migratedStarted && _migratedFailed);
			_concatedFound.finished = _apiFinished
				&& (!_migratedSearch || _migratedFinished)
				&& !_concatedFound.failed;
			_nextFounds.fire({});
		}
	}, _lifetime);

	if (_migratedSearch) {
		_migratedSearch->messagesFounds(
		) | rpl::on_next([=](const FoundMessages &data) {
			const auto replacingTotalOnly = _migratedStarted
				&& data.nextToken == _migratedFirstFound.nextToken
				&& _migratedFirstFound.totalOnly;
			if (!_migratedStarted
				|| data.nextToken != _migratedFirstFound.nextToken
				|| _migratedFirstFound.totalOnly) {
				if (replacingTotalOnly) {
					_migratedAdded = false;
				}
				_migratedStarted = true;
				_migratedFinished = data.finished;
				_migratedFailed = data.failed;
				_migratedFirstFound = data;
				if (_apiStarted && !_apiFinished) {
					_concatedFound.total = (_concatedFound.total >= 0
						&& data.total >= 0)
						? _concatedFound.total + data.total
						: -1;
					_concatedFound.failed = _apiFailed || _migratedFailed;
					_newFounds.fire({});
				} else if (_apiStarted && !_migratedAdded) {
					_concatedFound.total = (_concatedFound.total >= 0
						&& data.total >= 0)
						? _concatedFound.total + data.total
						: -1;
					addMigratedFirstFound();
					_concatedFound.failed = _apiFailed || _migratedFailed;
					_concatedFound.finished = _migratedFinished
						&& !_concatedFound.failed;
					_nextFounds.fire({});
				}
				return;
			}
			_migratedFinished = data.finished;
			_migratedFailed = data.failed;
			if (_apiFinished) {
				_concatedFound.total = (_concatedFound.total >= 0
					&& _migratedFirstFound.total >= 0
					&& data.total >= 0)
					? _concatedFound.total - _migratedFirstFound.total
						+ data.total
					: -1;
				addFound(data);
				_concatedFound.failed = _apiFailed || _migratedFailed;
				_concatedFound.finished = _migratedFinished
					&& !_concatedFound.failed;
				_nextFounds.fire({});
			}
		}, _lifetime);
	}
}

void MessagesSearchMerged::disableMigrated() {
	_migratedSearch = std::nullopt;
	_migratedStarted = false;
	_migratedFinished = true;
	_migratedFailed = false;
	_migratedAdded = true;
	_concatedFound.failed = _apiFailed;
	_concatedFound.finished = _apiFinished && !_apiFailed;
}

void MessagesSearchMerged::addFound(const FoundMessages &data) {
	for (const auto &message : data.messages) {
		_concatedFound.messages.push_back(message);
	}
	if (data.finished || data.failed || !data.messages.empty()) {
		_concatedFound.totalOnly = false;
	} else if (data.totalOnly && _concatedFound.messages.empty()) {
		_concatedFound.totalOnly = true;
	}
	_concatedFound.failed = _concatedFound.failed || data.failed;
}

void MessagesSearchMerged::addMigratedFirstFound() {
	if (_migratedAdded || !_migratedStarted) {
		return;
	}
	addFound(_migratedFirstFound);
	_migratedAdded = true;
}

const FoundMessages &MessagesSearchMerged::messages() const {
	return _concatedFound;
}

const MessagesSearch::Request &MessagesSearchMerged::request() const {
	return _request;
}

void MessagesSearchMerged::clear() {
	_concatedFound = {};
	_migratedFirstFound = {};
	_apiStarted = false;
	_apiFinished = false;
	_apiFailed = false;
	_migratedStarted = false;
	_migratedFinished = false;
	_migratedFailed = false;
	_migratedAdded = false;
}

void MessagesSearchMerged::search(const Request &search) {
	_request = search;
	clear();
	if (_migratedSearch) {
		_migratedSearch->searchMessages(search);
	}
	_apiSearch.searchMessages(search);
}

void MessagesSearchMerged::searchMore() {
	if (_apiFailed || _migratedFailed) {
		return;
	} else if (!_apiFinished) {
		_apiSearch.searchMore();
	} else if (_migratedSearch && !_migratedFinished) {
		_migratedSearch->searchMore();
	}
}

rpl::producer<> MessagesSearchMerged::newFounds() const {
	return _newFounds.events();
}

rpl::producer<> MessagesSearchMerged::nextFounds() const {
	return _nextFounds.events();
}

} // namespace Api
