import type { Metadata } from 'next';
import { Geist, Geist_Mono } from 'next/font/google';
import './globals.css';

const geistSans = Geist({
  variable: '--font-geist-sans',
  subsets: ['latin'],
});

const geistMono = Geist_Mono({
  variable: '--font-geist-mono',
  subsets: ['latin'],
});

export const metadata: Metadata = {
  title: 'NU-40 PET — 보드로 키우는 다마고치',
  description: 'NU-40 DK 버튼으로 밥을 주고 RGB LED로 기분을 확인하는 Bluetooth 다마고치',
  openGraph: {
    title: 'NU-40 PET',
    description: '보드로 키우는 다마고치',
    images: ['/og.png'],
  },
  twitter: {
    card: 'summary_large_image',
    title: 'NU-40 PET',
    description: '보드로 키우는 다마고치',
    images: ['/og.png'],
  },
};

export default function RootLayout({
  children,
}: Readonly<{
  children: React.ReactNode;
}>) {
  return (
    <html lang="ko">
      <body
        className={`${geistSans.variable} ${geistMono.variable} antialiased`}
      >
        {children}
      </body>
    </html>
  );
}
