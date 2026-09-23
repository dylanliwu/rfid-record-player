# RFID Record Player 🎵

A physical record player that plays music by scanning RFID-tagged "records". Tap a tag, and it streams the matching track straight from Spotify. Built on an ESP32-S3 with an RC522 RFID reader, DFPlayer Mini, and a custom motor driver circuit, with full Spotify Web API OAuth handled entirely on-device (no companion app).

Highlights:
On-device OAuth flow for Spotify Web API, no cloud middleman.
Custom power delivery via LM2596 buck converter, debugged through a tricky brownout issue caused by current spikes on the shared rail.
Physical RFID tap-to-play interface for an analog, tactile listening experience.
