/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "history/view/history_view_object.h"

namespace Data {
class Reactions;
class CloudImageView;
} // namespace Data

namespace Ui {
struct ChatPaintContext;
} // namespace Ui

namespace HistoryView {
using PaintContext = Ui::ChatPaintContext;
class Message;
struct TextState;
struct ReactionAnimationArgs;
struct UserpicInRow;
} // namespace HistoryView

namespace HistoryView::Reactions {

class Animation;

struct InlineListData {
	enum class Flag : uchar {
		InBubble  = 0x01,
		OutLayout = 0x02,
		Flipped   = 0x04,
	};
	friend inline constexpr bool is_flag_type(Flag) { return true; };
	using Flags = base::flags<Flag>;

	base::flat_map<QString, int> reactions;
	base::flat_map<QString, std::vector<not_null<PeerData*>>> recent;
	QString chosenReaction;
	Flags flags = {};
};

class InlineList final : public Object {
public:
	using Data = InlineListData;
	InlineList(
		not_null<::Data::Reactions*> owner,
		Fn<ClickHandlerPtr(QString)> handlerFactory,
		Data &&data);
	~InlineList();

	void update(Data &&data, int availableWidth);
	QSize countCurrentSize(int newWidth) override;
	[[nodiscard]] int countNiceWidth() const;
	[[nodiscard]] int placeAndResizeGetHeight(QRect available);
	void flipToRight();

	void updateSkipBlock(int width, int height);
	void removeSkipBlock();

	void paint(
		Painter &p,
		const PaintContext &context,
		int outerWidth,
		const QRect &clip) const;
	[[nodiscard]] bool getState(
		QPoint point,
		not_null<TextState*> outResult) const;

	void animate(
		ReactionAnimationArgs &&args,
		Fn<void()> repaint);
	[[nodiscard]] auto takeAnimations()
		-> base::flat_map<QString, std::unique_ptr<Reactions::Animation>>;
	void continueAnimations(base::flat_map<
		QString,
		std::unique_ptr<Reactions::Animation>> animations);

private:
	struct Userpics {
		QImage image;
		std::vector<UserpicInRow> list;
		bool someNotLoaded = false;
	};
	struct Button {
		QRect geometry;
		mutable std::unique_ptr<Animation> animation;
		mutable QImage image;
		mutable ClickHandlerPtr link;
		std::unique_ptr<Userpics> userpics;
		QString emoji;
		QString countText;
		int count = 0;
		int countTextWidth = 0;
	};

	void layout();
	void layoutButtons();

	void setButtonCount(Button &button, int count);
	void setButtonUserpics(
		Button &button,
		const std::vector<not_null<PeerData*>> &peers);
	[[nodiscard]] Button prepareButtonWithEmoji(const QString &emoji);
	void resolveUserpicsImage(const Button &button) const;

	QSize countOptimalSize() override;

	const not_null<::Data::Reactions*> _owner;
	const Fn<ClickHandlerPtr(QString)> _handlerFactory;
	Data _data;
	std::vector<Button> _buttons;
	QSize _skipBlock;

};

[[nodiscard]] InlineListData InlineListDataFromMessage(
	not_null<Message*> message);

} // namespace HistoryView
