/*
 * Strawberry Music Player
 * Copyright 2020-2023, Jonas Kvinge <jonas@jkvinge.net>
 *
 * Strawberry is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Strawberry is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Strawberry.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QString>
#include <QPixmap>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QKeySequence>
#include <QCheckBox>
#include <QSettings>

#include "utilities/screenutils.h"
#include "messagedialog.h"
#include "ui_messagedialog.h"

MessageDialog::MessageDialog(QWidget *parent) : QDialog(parent), ui_(new Ui_MessageDialog), parent_(parent) {

  ui_->setupUi(this);

  setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

  ui_->buttonBox->button(QDialogButtonBox::Ok)->setShortcut(QKeySequence::Close);

  QObject::connect(ui_->checkbox_do_not_show_message_again, &QCheckBox::toggled, this, &MessageDialog::DoNotShowMessageAgain);

}

MessageDialog::~MessageDialog() { delete ui_; }

void MessageDialog::ShowMessage(const QString &title, const QString &message, const QIcon &icon) {

  setWindowTitle(title);

  if (!icon.isNull()) {
#if (QT_VERSION >= QT_VERSION_CHECK(6, 0, 0))
    const QPixmap pixmap = icon.pixmap(QSize(64, 64), devicePixelRatioF());
#else
    const QPixmap pixmap = icon.pixmap(QSize(64, 64));
#endif
    ui_->label_logo->setPixmap(pixmap);
  }

  ui_->label_text->setText(message);
  ui_->label_text->adjustSize();
  adjustSize();

  if (parent_) {
    Utilities::CenterWidgetOnScreen(Utilities::GetScreen(parent_), this);
  }

  show();

}

void MessageDialog::DoNotShowMessageAgain() {

  if (!settings_group_.isEmpty() && !do_not_show_message_again_.isEmpty()) {
    QSettings s;
    s.beginGroup(settings_group_);
    s.setValue(do_not_show_message_again_, ui_->checkbox_do_not_show_message_again->isChecked());
    s.endGroup();
  }

}
