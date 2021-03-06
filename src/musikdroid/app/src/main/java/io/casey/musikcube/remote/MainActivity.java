package io.casey.musikcube.remote;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.text.SpannableStringBuilder;
import android.text.TextPaint;
import android.text.method.LinkMovementMethod;
import android.text.style.ClickableSpan;
import android.text.style.ForegroundColorSpan;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewPropertyAnimator;
import android.widget.CheckBox;
import android.widget.CompoundButton;
import android.widget.ImageView;
import android.widget.TextView;

import com.bumptech.glide.Glide;
import com.bumptech.glide.load.resource.drawable.GlideDrawable;
import com.bumptech.glide.request.RequestListener;
import com.bumptech.glide.request.target.Target;

import org.json.JSONArray;
import org.json.JSONObject;

import java.util.HashMap;
import java.util.Map;

public class MainActivity extends WebSocketActivityBase {
    private static Map<TransportModel.RepeatMode, Integer> REPEAT_TO_STRING_ID;

    private WebSocketService wss = null;
    private TransportModel model = new TransportModel();

    private SharedPreferences prefs;
    private TextView title, artist, album, playPause, volume;
    private TextView titleWithArt, artistAndAlbumWithArt, volumeWithArt;
    private TextView notPlayingOrDisconnected;
    private View connected;
    private CheckBox shuffleCb, muteCb, repeatCb;
    private View disconnectedOverlay;
    private Handler handler = new Handler();

    /* ugh, artwork related */
    private enum DisplayMode { Artwork, NoArtwork, Stopped }
    private View mainTrackMetadataWithAlbumArt, mainTrackMetadataNoAlbumArt;
    private ViewPropertyAnimator metadataAnim1, metadataAnim2;
    private AlbumArtModel albumArtModel = new AlbumArtModel();
    private ImageView albumArtImageView;

    static {
        REPEAT_TO_STRING_ID = new HashMap<>();
        REPEAT_TO_STRING_ID.put(TransportModel.RepeatMode.None, R.string.button_repeat_off);
        REPEAT_TO_STRING_ID.put(TransportModel.RepeatMode.List, R.string.button_repeat_list);
        REPEAT_TO_STRING_ID.put(TransportModel.RepeatMode.Track, R.string.button_repeat_track);
    }

    public static Intent getStartIntent(final Context context) {
        return new Intent(context, MainActivity.class)
            .addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP | Intent.FLAG_ACTIVITY_CLEAR_TOP);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        this.prefs = this.getSharedPreferences("prefs", Context.MODE_PRIVATE);
        this.wss = getWebSocketService();

        setContentView(R.layout.activity_main);
        bindEventListeners();

        if (!this.wss.hasValidConnection()) {
            startActivity(SettingsActivity.getStartIntent(this));
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        unbindCheckboxEventListeners();
    }

    @Override
    protected void onResume() {
        super.onResume();
        bindCheckBoxEventListeners();
        rebindUi();
    }

    @Override
    protected void onRestoreInstanceState(Bundle savedInstanceState) {
        super.onRestoreInstanceState(savedInstanceState);
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        getMenuInflater().inflate(R.menu.main_menu, menu);
        return true;
    }

    @Override
    public boolean onPrepareOptionsMenu(Menu menu) {
        boolean connected = wss.getState() == WebSocketService.State.Connected;
        menu.findItem(R.id.action_playlists).setEnabled(connected);
        menu.findItem(R.id.action_genres).setEnabled(connected);
        return super.onPrepareOptionsMenu(menu);
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        switch (item.getItemId()) {
            case R.id.action_settings:
                startActivity(SettingsActivity.getStartIntent(this));
                return true;

            case R.id.action_genres:
                startActivity(CategoryBrowseActivity.getStartIntent(this, Messages.Category.GENRE));
                return true;

            case R.id.action_playlists:
                startActivity(CategoryBrowseActivity.getStartIntent(
                    this, Messages.Category.PLAYLISTS, CategoryBrowseActivity.DeepLink.TRACKS));
                return true;
        }

        return super.onOptionsItemSelected(item);
    }

    @Override
    protected WebSocketService.Client getWebSocketServiceClient() {
        return this.serviceClient;
    }

    private void bindCheckBoxEventListeners() {
        this.shuffleCb.setOnCheckedChangeListener(shuffleListener);
        this.muteCb.setOnCheckedChangeListener(muteListener);
        this.repeatCb.setOnCheckedChangeListener(repeatListener);
    }

    /* onRestoreInstanceState() calls setChecked(), which has the side effect of
    running these callbacks. this screws up state, especially for the repeat checkbox */
    private void unbindCheckboxEventListeners() {
        this.shuffleCb.setOnCheckedChangeListener(null);
        this.muteCb.setOnCheckedChangeListener(null);
        this.repeatCb.setOnCheckedChangeListener(null);
    }

    private void bindEventListeners() {
        this.title = (TextView) findViewById(R.id.track_title);
        this.artist = (TextView) findViewById(R.id.track_artist);
        this.album = (TextView) findViewById(R.id.track_album);
        this.volume = (TextView) findViewById(R.id.volume);

        this.titleWithArt = (TextView) findViewById(R.id.with_art_track_title);
        this.artistAndAlbumWithArt = (TextView) findViewById(R.id.with_art_artist_and_album);
        this.volumeWithArt = (TextView) findViewById(R.id.with_art_volume);

        this.playPause = (TextView) findViewById(R.id.button_play_pause);
        this.shuffleCb = (CheckBox) findViewById(R.id.check_shuffle);
        this.muteCb = (CheckBox) findViewById(R.id.check_mute);
        this.repeatCb = (CheckBox) findViewById(R.id.check_repeat);
        this.mainTrackMetadataWithAlbumArt = findViewById(R.id.main_track_metadata_with_art);
        this.mainTrackMetadataNoAlbumArt = findViewById(R.id.main_track_metadata_without_art);
        this.notPlayingOrDisconnected = (TextView) findViewById(R.id.main_not_playing);
        this.albumArtImageView = (ImageView) findViewById(R.id.album_art);
        this.connected = findViewById(R.id.connected);

        this.disconnectedOverlay = findViewById(R.id.disconnected_overlay);

        /* these will get faded in as appropriate */
        this.mainTrackMetadataNoAlbumArt.setAlpha(0.0f);
        this.mainTrackMetadataWithAlbumArt.setAlpha(0.0f);

        findViewById(R.id.button_prev).setOnClickListener((View view) ->
            wss.send(SocketMessage.Builder.request(
                Messages.Request.Previous).build()));

        final LongPressTextView seekBack = (LongPressTextView) findViewById(R.id.button_seek_back);
        seekBack.setOnTickListener((View view) ->
            wss.send(SocketMessage.Builder
                .request(Messages.Request.SeekRelative)
                .addOption(Messages.Key.DELTA, -5.0f).build()));

        findViewById(R.id.button_play_pause).setOnClickListener((View view) -> {
            if (model.getPlaybackState() == TransportModel.PlaybackState.Stopped) {
                wss.send(SocketMessage.Builder.request(
                    Messages.Request.PlayAllTracks).build());
            }
            else {
                wss.send(SocketMessage.Builder.request(
                    Messages.Request.PauseOrResume).build());
            }
        });

        findViewById(R.id.button_next).setOnClickListener((View view) -> {
            wss.send(SocketMessage.Builder.request(
                Messages.Request.Next).build());
        });

        final LongPressTextView seekForward = (LongPressTextView) findViewById(R.id.button_seek_forward);
        seekForward.setOnTickListener((View view) ->
            wss.send(SocketMessage.Builder
                .request(Messages.Request.SeekRelative)
                .addOption(Messages.Key.DELTA, 5.0f).build()));

        final LongPressTextView volumeUp = (LongPressTextView) findViewById(R.id.button_vol_up);
        volumeUp.setOnTickListener((View view) -> {
            wss.send(SocketMessage.Builder
                .request(Messages.Request.SetVolume)
                .addOption(Messages.Key.RELATIVE, Messages.Value.UP)
                .build());
        });

        final LongPressTextView volumeDown = (LongPressTextView) findViewById(R.id.button_vol_down);
        volumeDown.setOnTickListener((View view) -> {
            wss.send(SocketMessage.Builder
                .request(Messages.Request.SetVolume)
                .addOption(Messages.Key.RELATIVE, Messages.Value.DOWN)
                .build());
        });

        notPlayingOrDisconnected.setOnClickListener((view) -> {
            if (wss.getState() != WebSocketService.State.Connected) {
                wss.reconnect();
            }
        });

        findViewById(R.id.button_artists).setOnClickListener((View view) -> {
            startActivity(CategoryBrowseActivity.getStartIntent(this, Messages.Category.ALBUM_ARTIST));
        });

        findViewById(R.id.button_tracks).setOnClickListener((View view) -> {
            startActivity(TrackListActivity.getStartIntent(MainActivity.this));
        });

        findViewById(R.id.button_albums).setOnClickListener((View view) -> {
            startActivity(AlbumBrowseActivity.getStartIntent(MainActivity.this));
        });

        findViewById(R.id.button_play_queue).setOnClickListener((view) -> navigateToPlayQueue());

        findViewById(R.id.metadata_container).setOnClickListener((view) -> {
            if (model.getQueueCount() > 0) {
                navigateToPlayQueue();
            }
        });

        disconnectedOverlay.setOnClickListener((view) -> {
            /* swallow input so user can't click on things while disconnected */
        });

        this.album.setOnClickListener((view) -> navigateToCurrentAlbum());
        this.artist.setOnClickListener((view) -> navigateToCurrentArtist());
    }

    private void rebindAlbumArtistWithArtTextView() {
        final String artist = model.getTrackValueString(TransportModel.Key.ARTIST, "");
        final String album = model.getTrackValueString(TransportModel.Key.ALBUM, "");

        final ForegroundColorSpan albumColor =
            new ForegroundColorSpan(getResources().getColor(R.color.theme_orange));

        final ForegroundColorSpan artistColor =
            new ForegroundColorSpan(getResources().getColor(R.color.theme_yellow));

        final SpannableStringBuilder builder =
            new SpannableStringBuilder().append(album).append(" - ").append(artist);

        final ClickableSpan albumClickable = new ClickableSpan() {
            @Override
            public void onClick(View widget) {
                navigateToCurrentAlbum();
            }

            @Override
            public void updateDrawState(TextPaint ds) {
            }
        };

        final ClickableSpan artistClickable = new ClickableSpan() {
            @Override
            public void onClick(View widget) {
                navigateToCurrentArtist();
            }

            @Override
            public void updateDrawState(TextPaint ds) {
            }
        };

        int artistOffset = album.length() + 3;

        builder.setSpan(albumColor, 0, album.length(), 0);
        builder.setSpan(albumClickable, 0, album.length(), 0);
        builder.setSpan(artistColor, artistOffset, artistOffset + artist.length(), 0);
        builder.setSpan(artistClickable, artistOffset, artistOffset + artist.length(), 0);

        this.artistAndAlbumWithArt.setMovementMethod(LinkMovementMethod.getInstance());
        this.artistAndAlbumWithArt.setHighlightColor(Color.TRANSPARENT);
        this.artistAndAlbumWithArt.setText(builder);
    }

    private void rebindUi() {
        /* state management for UI stuff is starting to get out of hand. we should
        refactor things pretty soon before they're completely out of control */

        final WebSocketService.State state = wss.getState();

        final boolean connected = state == WebSocketService.State.Connected;

        final boolean playing = (model.getPlaybackState() == TransportModel.PlaybackState.Playing);
        this.playPause.setText(playing ? R.string.button_pause : R.string.button_play);

        final boolean stopped = (model.getPlaybackState() == TransportModel.PlaybackState.Stopped);
        notPlayingOrDisconnected.setVisibility(stopped ? View.VISIBLE : View.GONE);

        final boolean stateIsValidForArtwork = !stopped && connected && model.isValid();

        this.connected.setVisibility((connected && stopped) ? View.VISIBLE : View.GONE);
        this.disconnectedOverlay.setVisibility(connected ? View.GONE : View.VISIBLE);

        /* setup our state as if we have no album art -- because we don't know if we have any
        yet! the album art load process (if enabled) will ensure the correct metadata block
        is displayed in the correct location */
        if (!stateIsValidForArtwork) {
            setMetadataDisplayMode(DisplayMode.Stopped);
            notPlayingOrDisconnected.setText(connected ? R.string.transport_not_playing : R.string.status_disconnected);
            notPlayingOrDisconnected.setVisibility(View.VISIBLE);
        }
        else {
            notPlayingOrDisconnected.setVisibility(View.GONE);
        }

        final String artist = model.getTrackValueString(TransportModel.Key.ARTIST, "");
        final String album = model.getTrackValueString(TransportModel.Key.ALBUM, "");
        final String title = model.getTrackValueString(TransportModel.Key.TITLE, "");
        final String volume = getString(R.string.status_volume, Math.round(model.getVolume() * 100));

        this.title.setText(title);
        this.artist.setText(artist);
        this.album.setText(album);
        this.volume.setText(volume);

        this.rebindAlbumArtistWithArtTextView();
        this.titleWithArt.setText(title);
        this.volumeWithArt.setText(volume);

        final TransportModel.RepeatMode repeatMode = model.getRepeatMode();
        final boolean repeatChecked = (repeatMode != TransportModel.RepeatMode.None);
        repeatCb.setText(REPEAT_TO_STRING_ID.get(repeatMode));
        Views.setCheckWithoutEvent(repeatCb, repeatChecked, this.repeatListener);

        Views.setCheckWithoutEvent(this.shuffleCb, model.isShuffled(), this.shuffleListener);
        Views.setCheckWithoutEvent(this.muteCb, model.isMuted(), this.muteListener);

        boolean albumArtEnabledInSettings = this.prefs.getBoolean("album_art_enabled", true);

        if (stateIsValidForArtwork) {
            if (!albumArtEnabledInSettings) {
                this.albumArtModel = new AlbumArtModel();
                setMetadataDisplayMode(DisplayMode.NoArtwork);
            }
            else {
                if (!this.albumArtModel.is(artist, album)) {
                    this.albumArtModel.destroy();
                    this.albumArtModel = new AlbumArtModel(title, artist, album, albumArtRetrieved);
                }

                updateAlbumArt();
            }
        }
    }

    private void clearUi() {
        model.reset();
        albumArtModel = new AlbumArtModel();
        updateAlbumArt();
        rebindUi();
    }

    private void setMetadataDisplayMode(DisplayMode mode) {
        if (metadataAnim1 != null) {
            metadataAnim1.cancel();
            metadataAnim2.cancel();
        }

        if (mode == DisplayMode.Stopped) {
            albumArtImageView.setImageDrawable(null);
            metadataAnim1 = Views.animateAlpha(mainTrackMetadataWithAlbumArt, 0.0f);
            metadataAnim2 = Views.animateAlpha(mainTrackMetadataNoAlbumArt, 0.0f);
        }
        else if (mode == DisplayMode.Artwork) {
            metadataAnim1 = Views.animateAlpha(mainTrackMetadataWithAlbumArt, 1.0f);
            metadataAnim2 = Views.animateAlpha(mainTrackMetadataNoAlbumArt, 0.0f);
        }
        else {
            albumArtImageView.setImageDrawable(null);
            metadataAnim1 = Views.animateAlpha(mainTrackMetadataWithAlbumArt, 0.0f);
            metadataAnim2 = Views.animateAlpha(mainTrackMetadataNoAlbumArt, 1.0f);
        }
    }

    private void preloadNextImage() {
        final SocketMessage request = SocketMessage.Builder
                .request(Messages.Request.QueryPlayQueueTracks)
                .addOption(Messages.Key.OFFSET, this.model.getQueuePosition() + 1)
                .addOption(Messages.Key.LIMIT, 1)
                .build();

        this.wss.send(request, this.getWebSocketServiceClient(), (response) -> {
            final JSONArray data = response.getJsonArrayOption(Messages.Key.DATA, new JSONArray());
            if (data.length() > 0) {
                JSONObject track = data.optJSONObject(0);
                final String artist = track.optString(TransportModel.Key.ARTIST, "");
                final String album = track.optString(TransportModel.Key.ALBUM, "");

                if (!albumArtModel.is(artist, album)) {
                    new AlbumArtModel("", artist, album, (info, url) -> {
                        int width = albumArtImageView.getWidth();
                        int height = albumArtImageView.getHeight();
                        Glide.with(MainActivity.this).load(url).downloadOnly(width, height);
                    }).fetch();
                }
            }
        });
    }

    private void updateAlbumArt() {
        if (model.getPlaybackState() == TransportModel.PlaybackState.Stopped) {
            setMetadataDisplayMode(DisplayMode.NoArtwork);
        }

        final String url = albumArtModel.getUrl();

        if (Strings.empty(url)) {
            albumArtModel.fetch();
            setMetadataDisplayMode(DisplayMode.NoArtwork);
        }
        else {
            final int loadId = albumArtModel.getId();

            Glide.with(this)
                .load(url)
                .listener(new RequestListener<String, GlideDrawable>() {
                    @Override
                    public boolean onException(Exception e,
                                               String model,
                                               Target<GlideDrawable> target,
                                               boolean isFirstResource)
                    {
                        setMetadataDisplayMode(DisplayMode.NoArtwork);
                        return false;
                    }

                    @Override
                    public boolean onResourceReady(GlideDrawable resource,
                                                   String model,
                                                   Target<GlideDrawable> target,
                                                   boolean isFromMemoryCache,
                                                   boolean isFirstResource)
                    {
                        if (!isPaused()) {
                            preloadNextImage();
                        }

                        /* if the loadId doesn't match the current id, then the image was
                        loaded for a different song. throw it away. */
                        if (albumArtModel.getId() != loadId) {
                            return true;
                        }
                        else {
                            setMetadataDisplayMode(DisplayMode.Artwork);
                            return false;
                        }
                    }
                })
                .into(albumArtImageView);
        }
    }

    private void navigateToCurrentArtist() {
        final long artistId = model.getTrackValueLong(TransportModel.Key.ARTIST_ID, -1);
        if (artistId != -1) {
            final String artistName = model.getTrackValueString(TransportModel.Key.ARTIST, "");
            startActivity(AlbumBrowseActivity.getStartIntent(
                MainActivity.this, Messages.Category.ARTIST, artistId, artistName));
        }
    }

    private void navigateToCurrentAlbum() {
        final long albumId = model.getTrackValueLong(TransportModel.Key.ALBUM_ID, -1);
        if (albumId != -1) {
            final String albumName = model.getTrackValueString(TransportModel.Key.ALBUM, "");
            startActivity(TrackListActivity.getStartIntent(
                MainActivity.this, Messages.Category.ALBUM, albumId, albumName));
        }
    }

    private void navigateToPlayQueue() {
        startActivity(PlayQueueActivity.getStartIntent(MainActivity.this, model.getQueuePosition()));
    }

    private AlbumArtModel.AlbumArtCallback albumArtRetrieved = (model, url) -> {
        handler.post(() -> {
            if (model == albumArtModel) {
                updateAlbumArt();
            }
        });
    };

    private CheckBox.OnCheckedChangeListener muteListener =
        (CompoundButton compoundButton, boolean b) -> {
            if (b != model.isMuted()) {
                wss.send(SocketMessage.Builder
                    .request(Messages.Request.ToggleMute).build());
            }
        };

    private CheckBox.OnCheckedChangeListener shuffleListener =
        (CompoundButton compoundButton, boolean b) -> {
            if (b != model.isShuffled()) {
                wss.send(SocketMessage.Builder
                    .request(Messages.Request.ToggleShuffle).build());
            }
        };

    final CheckBox.OnCheckedChangeListener repeatListener = new CompoundButton.OnCheckedChangeListener() {
        @Override
        public void onCheckedChanged(CompoundButton compoundButton, boolean b) {
            final TransportModel.RepeatMode currentMode = model.getRepeatMode();

            TransportModel.RepeatMode newMode = TransportModel.RepeatMode.None;

            if (currentMode == TransportModel.RepeatMode.None) {
                newMode = TransportModel.RepeatMode.List;
            }
            else if (currentMode == TransportModel.RepeatMode.List) {
                newMode = TransportModel.RepeatMode.Track;
            }

            final boolean checked = (newMode != TransportModel.RepeatMode.None);
            compoundButton.setText(REPEAT_TO_STRING_ID.get(newMode));
            Views.setCheckWithoutEvent(repeatCb, checked, this);

            wss.send(SocketMessage.Builder
                .request(Messages.Request.ToggleRepeat)
                .build());
        }
    };

    private WebSocketService.Client serviceClient = new WebSocketService.Client() {
        @Override
        public void onStateChanged(WebSocketService.State newState, WebSocketService.State oldState) {
            if (newState == WebSocketService.State.Connected) {
                wss.send(SocketMessage.Builder.request(
                    Messages.Request.GetPlaybackOverview.toString()).build());

                rebindUi();
            }
            else if (newState == WebSocketService.State.Disconnected) {
                clearUi();
            }
        }

        @Override
        public void onMessageReceived(SocketMessage message) {
            if (model.canHandle(message)) {
                if (model.update(message)) {
                    rebindUi();
                }
            }
        }

        @Override
        public void onInvalidPassword() {
            final String tag = InvalidPasswordDialogFragment.TAG;
            if (getSupportFragmentManager().findFragmentByTag(tag) == null) {
                InvalidPasswordDialogFragment
                    .newInstance().show(getSupportFragmentManager(), tag);
            }
        }
    };
}
