import { WEB_APP_VERSION, WEB_BUILD } from '../version';

export function WebAppFooter() {
  return (
    <footer className="app-footer">
      <span>NuWallet Web v{WEB_APP_VERSION}</span>
      <span>Build {WEB_BUILD}</span>
      <span>BLE Protocol v1</span>
      <span>Prototype · 실제 자금 사용 금지</span>
    </footer>
  );
}
