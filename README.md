# XIAO ESP32-S3 Tailscale Camera

Seeed XIAO ESP32-S3 Sense用のカメラファームウェアです。初回設定とUSB書き込みにはDockerを使います。書き込み後はESP32本体だけで動作するため、映像を見るときにPCやDockerを起動しておく必要はありません。

## できること

- ブラウザーから最大10fpsのライブ映像を表示
- 新しい端末が開くと、その端末へ視聴を切り替え
- 自動露光、自動ゲイン、オートホワイトバランス
- Tailscale経由で外出先からアクセス

暗所では露光補正を上げていますが、映像がザラついたり動くものがぼやけたりすることがあります。光がまったくない場所では照明が必要です。

## 用意するもの

- Seeed XIAO ESP32-S3 Sense（カメラ付き）とデータ通信対応USBケーブル
- 2.4GHz Wi-Fi
- DockerとDocker Composeが使えるLinux PC
- Tailscaleアカウント

## 初回設定

1. このリポジトリをダウンロードするか、クローンします。

   ```sh
   git clone https://github.com/HidemitsuNishioka/xiao-esp32s3-tailscale-camera.git
   cd xiao-esp32s3-tailscale-camera
   ```

2. 設定ファイルのひな形を複製します。

   ```sh
   cp .env.example .env
   cp secrets.yaml.example secrets.yaml
   ```

3. `secrets.yaml` をテキストエディターで開き、`REPLACE_ME` や `YOUR_...` の項目を自分の情報に置き換えます。
   - `wifi_ssid` と `wifi_password`: 2.4GHz Wi-Fiの情報
   - `tailscale_auth_key`: [Tailscaleの認証キー](https://tailscale.com/docs/features/access-control/auth-keys)。設定ファイルにだけ保存し、他人と共有しないでください。
   - `api_encryption_key`: ターミナルで `openssl rand -base64 32` を実行し、表示された文字列を設定します。
   - `web_username` と `web_password`: ESPHome状態ページ用に自分で決めます。

   `.env` と `secrets.yaml` はGitの対象外です。実際の認証情報をREADMEやソースコードへ書き込まないでください。

4. XIAOをUSB接続し、プロジェクトフォルダーで設定を確認してから書き込みます。

   ```sh
   docker compose run --rm esphome config xiao-esp32s3-tailscale-camera.yaml
   docker compose run --rm esphome run xiao-esp32s3-tailscale-camera.yaml --device /dev/ttyACM0
   ```

   書き込み後にESPHomeのログ画面が開いたままになることがあります。ログでWi-Fi/Tailscaleへの接続を確認したら `Ctrl+C` で閉じて構いません。ファームウェアはESP32上で動き続けます。

## 映像を見る

端末を同じTailscaleネットワークに接続し、Tailscale管理画面でカメラのIPアドレスを確認します。ブラウザーで次を開きます。

```text
http://<カメラのTailscale IP>:8082/
```

ライブ映像は1端末ずつ視聴できます。後から接続した端末を優先し、前の端末には切断理由を表示します。

## Wi-Fiを変更する

ローカルの `secrets.yaml` にある `wifi_ssid` と `wifi_password` を変更し、XIAOをUSB接続して上記の書き込みコマンドを再実行します。Wi-Fiは2.4GHzに接続してください。

## 困ったとき

- **USBデバイスが見つからない:** データ通信対応ケーブルを使い、`.env` の `ESP32_DEVICE` をPC上のデバイス名に合わせます。初期値は `/dev/ttyACM0` です。
- **Wi-Fiにつながらない:** 2.4GHz対応のネットワークか、SSIDとパスワードに入力間違いがないかを確認します。
- **映像が開かない:** 閲覧端末も同じTailscaleネットワークに接続されているか、カメラのTailscale IPが正しいかを確認します。
- **暗所で見えにくい:** 周囲に照明を追加してください。暗いほどノイズや動きのぼやけが増えます。

## ポートとアクセス

- `8082`: ブラウザー向けライブ映像。Tailscale IPv4からの接続を受け付けます。
- `8081`: 静止画。ログイン認証はありません。ポートをインターネットへ公開せず、信頼できるネットワーク内で使用してください。
- `80`: ESPHome状態ページ。`secrets.yaml` で設定したユーザー名とパスワードが必要です。

この設定は個人用Tailscale外部コンポーネントを利用します。Tailscale公式クライアントではありません。
