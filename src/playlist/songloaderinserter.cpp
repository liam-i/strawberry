/*
 * Strawberry Music Player
 * This file was part of Clementine.
 * Copyright 2010, David Sansome <me@davidsansome.com>
 * Copyright 2019-2021, Jonas Kvinge <jonas@jkvinge.net>
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

#include <QtConcurrent>
#include <QtAlgorithms>
#include <QList>
#include <QUrl>

#include "core/logging.h"
#include "core/songloader.h"
#include "core/taskmanager.h"
#include "playlist.h"
#include "songloaderinserter.h"

SongLoaderInserter::SongLoaderInserter(TaskManager *task_manager, CollectionBackendInterface *collection, const Player *player, QObject *parent)
    : QObject(parent),
      task_manager_(task_manager),
      destination_(nullptr),
      row_(-1),
      play_now_(true),
      enqueue_(false),
      enqueue_next_(false),
      collection_(collection),
      player_(player) {}

SongLoaderInserter::~SongLoaderInserter() { qDeleteAll(pending_); }

void SongLoaderInserter::Load(Playlist *destination, int row, bool play_now, bool enqueue, bool enqueue_next, const QList<QUrl> &urls) {

  destination_ = destination;
  row_ = row;
  play_now_ = play_now;
  enqueue_ = enqueue;
  enqueue_next_ = enqueue_next;

  QObject::connect(destination, &Playlist::destroyed, this, &SongLoaderInserter::DestinationDestroyed);
  QObject::connect(this, &SongLoaderInserter::PreloadFinished, this, &SongLoaderInserter::InsertSongs);
  QObject::connect(this, &SongLoaderInserter::EffectiveLoadFinished, destination, &Playlist::UpdateItems);

  for (const QUrl &url : urls) {
    SongLoader *loader = new SongLoader(collection_, player_, this);

    SongLoader::Result ret = loader->Load(url);

    if (ret == SongLoader::Result::BlockingLoadRequired) {
      pending_.append(loader);
      continue;
    }

    if (ret == SongLoader::Result::Success) {
      songs_ << loader->songs();
    }
    else {
      for (const QString &error : loader->errors()) {
        emit Error(error);
      }
    }
    delete loader;
  }

  if (pending_.isEmpty()) {
    InsertSongs();
    deleteLater();
  }
  else {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    (void)QtConcurrent::run(&SongLoaderInserter::AsyncLoad, this);
#else
    (void)QtConcurrent::run(this, &SongLoaderInserter::AsyncLoad);
#endif
  }
}

// Load audio CD tracks:
// First, we add tracks (without metadata) into the playlist
// In the meantime, MusicBrainz will be queried to get songs' metadata.
// AudioCDTagsLoaded will be called next, and playlist's items will be updated.
void SongLoaderInserter::LoadAudioCD(Playlist *destination, int row, bool play_now, bool enqueue, bool enqueue_next) {

  destination_ = destination;
  row_ = row;
  play_now_ = play_now;
  enqueue_ = enqueue;
  enqueue_next_ = enqueue_next;

  SongLoader *loader = new SongLoader(collection_, player_, this);
  QObject::connect(loader, &SongLoader::AudioCDTracksLoadFinished, this, [this, loader]() { AudioCDTracksLoadFinished(loader); });
  QObject::connect(loader, &SongLoader::LoadAudioCDFinished, this, &SongLoaderInserter::AudioCDTagsLoaded);
  qLog(Info) << "Loading audio CD...";
  SongLoader::Result ret = loader->LoadAudioCD();
  if (ret == SongLoader::Result::Error) {
    if (loader->errors().isEmpty())
      emit Error(tr("Error while loading audio CD."));
    else {
      for (const QString &error : loader->errors()) {
        emit Error(error);
      }
    }
    delete loader;
  }
  // Songs will be loaded later: see AudioCDTracksLoadFinished and AudioCDTagsLoaded slots

}

void SongLoaderInserter::DestinationDestroyed() { destination_ = nullptr; }

void SongLoaderInserter::AudioCDTracksLoadFinished(SongLoader *loader) {

  songs_ = loader->songs();
  if (songs_.isEmpty()) {
    for (const QString &error : loader->errors()) {
      emit Error(error);
    }
  }
  else {
    InsertSongs();
  }

}

void SongLoaderInserter::AudioCDTagsLoaded(const bool success) {

  SongLoader *loader = qobject_cast<SongLoader*>(sender());
  if (!loader || !destination_) return;

  if (success) {
    destination_->UpdateItems(loader->songs());
  }
  else {
    qLog(Error) << "Error while getting audio CD metadata from MusicBrainz";
  }

  deleteLater();

}

void SongLoaderInserter::InsertSongs() {

  // Insert songs (that haven't been completely loaded) to allow user to see and play them while not loaded completely
  if (destination_) {
    destination_->InsertSongsOrCollectionItems(songs_, row_, play_now_, enqueue_, enqueue_next_);
  }

}

void SongLoaderInserter::AsyncLoad() {

  // First, quick load raw songs.
  int async_progress = 0;
  int async_load_id = task_manager_->StartTask(tr("Loading tracks"));
  task_manager_->SetTaskProgress(async_load_id, async_progress, pending_.count());
  bool first_loaded = false;
  for (int i = 0; i < pending_.count(); ++i) {
    SongLoader *loader = pending_[i];
    SongLoader::Result res = loader->LoadFilenamesBlocking();
    task_manager_->SetTaskProgress(async_load_id, ++async_progress);

    if (res == SongLoader::Result::Error) {
      for (const QString &error : loader->errors()) {
        emit Error(error);
      }
      continue;
    }

    if (!first_loaded) {
      // Load everything from the first song.
      // It'll start playing as soon as we emit PreloadFinished, so it needs to have the duration set to show properly in the UI.
      loader->LoadMetadataBlocking();
      first_loaded = true;
    }

    songs_ << loader->songs();

  }
  task_manager_->SetTaskFinished(async_load_id);
  emit PreloadFinished();

  // Songs are inserted in playlist, now load them completely.
  async_progress = 0;
  async_load_id = task_manager_->StartTask(tr("Loading tracks info"));
  task_manager_->SetTaskProgress(async_load_id, async_progress, songs_.count());
  SongList songs;
  for (int i = 0; i < pending_.count(); ++i) {
    SongLoader *loader = pending_[i];
    if (i != 0) {
      // We already did this earlier for the first song.
      loader->LoadMetadataBlocking();
    }
    songs << loader->songs();
    task_manager_->SetTaskProgress(async_load_id, songs.count());
  }
  task_manager_->SetTaskFinished(async_load_id);

  // Replace the partially-loaded items by the new ones, fully loaded.
  emit EffectiveLoadFinished(songs);

  deleteLater();

}
