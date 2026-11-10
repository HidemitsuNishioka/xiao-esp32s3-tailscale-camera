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

- ブラウザー向けカメラ画面: `http://100.x.y.z:8082/`
- 静止画: `http://100.x.y.z:8081/`
- ESPHome状態ページ: `http://100.x.y.z/`

カメラ画面はブラウザーで開けるため、VLCなどの追加アプリは不要です。映像は同時に1端末だけ視聴でき、新しい端末からの要求を優先します。先に見ていた端末は切断され、理由を表示する画面に移ります。カメラ画面はTailnetのIPv4アドレスからの接続だけを受け付けます。カメラの上限は10fpsですが、実際の速度はWi-Fiの状態で変わります。

カメラは自動露光、自動ゲイン、オートホワイトバランスで動作します。暗所で明るく映るよう露光補正を +2、自動ゲイン上限を 16x に設定しています。暗い場所では映像がザラついたり、動くものがぼやけたりすることがあります。明るさや光源の色が変わると、映像が落ち着くまで数秒かかることがあります。

カメラ画面（8082）はTailnetのIPv4アドレス範囲からの接続だけを許可しています。静止画ポート（8081）にはログイン認証がないため、Tailnet ACLとWi-Fi側ネットワークのアクセス制御も設定してください。

## 注意

- ESPHome Tailscale外部コンポーネント v0.5.31を利用しています。Tailscaleの機能はTailnet内で使う個人用途向けで、公式Tailscaleクライアントではありません。
- 対象はカメラ搭載のXIAO ESP32-S3 Senseです。カメラ拡張基板がない通常版ではカメラ機能は動きません。
- Tailscaleキーを含む `secrets.yaml` や `.env` はGitへ登録しないでください。
