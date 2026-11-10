# XIAO ESP32-S3 Tailscale Camera

Seeed XIAO ESP32-S3 Sense向けのESPHomeファームウェアです。カメラ映像はESP32本体から直接配信し、Tailscale経由でアクセスできます。DockerはビルドとUSB書き込みにだけ使います。書き込み後、映像を見るためにPCやDockerを起動しておく必要はありません。

## 初回準備

1. `.env.example` を `.env` にコピーし、必要ならシリアルデバイスを変更します。
2. `secrets.yaml.example` を `secrets.yaml` にコピーします。
3. `secrets.yaml` に2.4GHz Wi-FiのSSID・パスワード、Tailscale認証キー、API暗号鍵、Web UI用パスワードを設定します。認証キーはTailscale管理画面で作成してください。`secrets.yaml` はGit管理対象外です。
4. XIAO ESP32-S3 Senseのカメラ拡張基板とアンテナを取り付け、USBで接続します。

API暗号鍵は任意の端末で `openssl rand -base64 32` を実行して作成できます。
OTA更新はAPI暗号鍵を使って暗号化します。

## Dockerでビルド・書き込み

```sh
docker compose run --rm esphome config xiao-esp32s3-tailscale-camera.yaml
docker compose run --rm esphome run xiao-esp32s3-tailscale-camera.yaml --device /dev/ttyACM0
```

初回書き込みはUSB経由です。書き込み後、Tailscale管理画面に `xiao-camera` が登録されたことを確認し、必要ならデバイスのキー期限を無効にします。以降の更新は同じ設定からOTAで行えます。

## 映像へのアクセス

Tailscaleで割り当てられたIPアドレスを `100.x.y.z` とすると、同じTailnet上から次のURLを開きます。

- 複数端末向けライブ映像（VLCなどのRTSP対応アプリ）: `rtsp://100.x.y.z:554/`
- ブラウザー向けライブ映像: `http://100.x.y.z:8080/`
- 静止画: `http://100.x.y.z:8081/`
- ESPHome状態ページ: `http://100.x.y.z/`

RTSPは最大4端末まで同時に視聴できます。端末のVLCで「ネットワークストリームを開く」を選び、RTSP URLを入力してください。RTSPはブラウザーでは再生できません。カメラの上限は10fpsですが、実際の速度はWi-Fiの状態や視聴台数で変わります。従来のブラウザー向けライブ映像（8080）は同時に1接続までです。

RTSPはTailnetのIPv4アドレス範囲からの接続だけを許可しています。RTSPとカメラWebサーバーには映像用のログイン認証がないため、Tailnet ACLとWi-Fi側のアクセス制御も設定してください。

## 注意

- ESPHome Tailscale外部コンポーネント v0.5.31を利用しています。Tailscaleの機能はTailnet内で使う個人用途向けで、公式Tailscaleクライアントではありません。
- ESPHome 2026.11.0b3のRTSP機能を利用しています。試験版のため、安定版が公開されたら更新できます。
- 対象はカメラ搭載のXIAO ESP32-S3 Senseです。カメラ拡張基板がない通常版ではカメラ機能は動きません。
- Tailscaleキーを含む `secrets.yaml` や `.env` はGitへ登録しないでください。
