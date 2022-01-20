#include "logout.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "data/data_session.h"

void FakePasscode::LogoutAction::Execute() {
    std::vector<bool> new_logout;
    new_logout.reserve(Core::App().domain().accounts().size());
    for (const auto &[index, account] : Core::App().domain().accounts()) {
        if (index_to_logout_[index]) {
            account->loggedOut();
            account->mtpLogOut(false);
            index_to_logout_.remove(index);
            if (account->sessionExists()) {
                account->session().data().cache().clear();
                account->session().data().cacheBigFile().clear();
                Ui::Emoji::ClearIrrelevantCache();
            }
        }
    }
}

QByteArray FakePasscode::LogoutAction::Serialize() const {
    QByteArray result;
    QDataStream stream(&result, QIODevice::ReadWrite);
    stream << static_cast<qint32>(ActionType::Logout);
    QByteArray inner_data;
    QDataStream inner_stream(&inner_data, QIODevice::ReadWrite);
    for (const auto&[index, is_logged_out] : index_to_logout_) {
        if (is_logged_out) {
            inner_stream << index;
        }
    }
    stream << inner_data;
    return result;
}

FakePasscode::ActionType FakePasscode::LogoutAction::GetType() const {
    return ActionType::Logout;
}

FakePasscode::LogoutAction::LogoutAction(QByteArray inner_data) {
    DEBUG_LOG(("Create logout"));
    if (!inner_data.isEmpty()) {
        QDataStream stream(&inner_data, QIODevice::ReadOnly);
        while (!stream.atEnd()) {
            qint32 index;
            stream >> index;
            index_to_logout_[index] = true;
        }
    }
}

FakePasscode::LogoutAction::LogoutAction(base::flat_map<qint32, bool> index_to_logout)
: index_to_logout_(std::move(index_to_logout))
{
}

void FakePasscode::LogoutAction::SetLogout(qint32 index, bool logout) {
    index_to_logout_[index] = logout;
}

bool FakePasscode::LogoutAction::IsLogout(qint32 index) const {
    if (auto pos = index_to_logout_.find(index); pos != index_to_logout_.end()) {
        return pos->second;
    } else {
        return false;
    }
}

const base::flat_map<qint32, bool>& FakePasscode::LogoutAction::GetLogout() const {
    return index_to_logout_;
}

void FakePasscode::LogoutAction::SubscribeOnLoggingOut() {
    for (const auto&[index, account] : Core::App().domain().accounts()) {
        account->sessionChanges() | rpl::map([index = index, this] (Main::Session* session) {
            return session == nullptr ? index_to_logout_.erase(index) : -1;
        });
    }
}
