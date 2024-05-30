/*
 * log data converter
 *
 * Copyright (C) 2024 Hiroshi Kuwagata <kgt9221@gmail.com>
 */

///
/// コマンドラインオプション処理モジュール
///

use std::io::{Write, BufWriter, Error};
use std::fs::File;

use clap::Parser;
use regex::Regex;
use chrono::{NaiveDateTime, Utc, TimeZone};
use chrono_tz::Tz;

///
/// UNIX時刻への変換
///
/// # 引数
/// `s` - 記録開始時刻を表した文字列
/// `tz` - タイムゾーン情報
///
/// # 戻り値
/// 引数で指定された記録開始時刻を表した文字列と、タイムゾーン情報を元に算出し
/// たミリ秒単位のUNIX時刻をOk()でラップして返す。指定された、文字列が記録開始
/// 時刻としてパースできなかった場合はErr(())を返す。
///
fn try_convert_to_unix_mills(s: String, tz: Tz) -> Result<i64,()>
{
    match NaiveDateTime::parse_from_str(&s, "%Y%m%d %H%M%S") {
        Ok(time) => {
            let local = tz.from_local_datetime(&time).unwrap();
            let utc = Utc.from_utc_datetime(&local.naive_utc());

            Ok(utc.timestamp_millis())
        }

        Err(_) => Err(())
    }
}

/// コマンドラインオプションの解析結果を格納する構造体
#[derive(Parser, Debug)]
#[command(version, about = "Converter for DC power logger", long_about = None)]
pub struct Options {
    /// 出力先
    #[arg(short, long = "output",
        value_name = "FILE", help = "Set output file name")]
    output_file: Option<String>,

    /// 適用するタイムゾーン
    #[arg(short, long = "timezone", default_value = "Asia/Tokyo",
        value_name = "ZONE-NAME", help = "Set applicable time zone")]
    timezone: String,

    /// 入力ファイル名
    #[clap(help = "input file name")]
    input_file: String,
}

/// Optionsの実装
impl Options {
    ///
    /// タイムゾーン情報の取得
    ///
    /// # 戻り値
    /// コマンドラインオプションで指定されたタイムゾーン文字列が既知のものであ
    /// る場合は、タイムゾーン情報をOk()でラップして返す。未知のもの（不正なも
    /// の）の場合はErr(())を返す。
    ///
    fn get_timezone(&self) -> Result<Tz, ()> {
        match self.timezone.parse::<Tz>() {
            Ok(tz) => Ok(tz),
            Err(_) => {
                eprintln!("Invalid timezone string: {}", self.timezone);
                Err(())
            }
        }
    }

    ///
    /// 時刻情報文字列の取得
    ///
    /// # 戻り値
    /// 入力ファイル名に時刻情報が含まれている場合は、その箇所を切り出した文字
    /// 列をSome()でラップして返す。含まれていない場合はNoneを返す。
    ///
    fn get_timeinfo(&self) -> Option<String> {
        let re = Regex::new(r"powerlog-(\d{8})-(\d{6})\.dat").unwrap();

        if let Some(captures) = re.captures(&self.input_file) {
            let date = &captures[1];
            let time = &captures[2];

            Some(
                format!(
                    "{}{}{} {}{}{}",
                     &date[0..4],
                     &date[4..6],
                     &date[6..8],
                     &time[0..2],
                     &time[2..4],
                     &time[4..6]
                 )
            )
        } else {
            None
        }
    }

    ///
    /// 変換結果の出力先の取得
    ///
    /// # 戻り値
    /// 変換結果の出力先のオープンに成功した場合は、std::io::Writeクレートを実
    /// 装したオブジェクトをOk()でラップして返す。失敗した場合はstd::io::Error
    /// をErr()でラップして返す。
    ///
    /// # 注意事項
    /// 出力先は、アペンドモードではなく新規作成モードでオープンされる。このた
    /// め指定されているファイルが既存の場合は、内容が削除されるので注意するこ
    /// と。
    ///
    pub fn get_output_writer(&self) -> Result<Box<dyn Write>, Error> {
        let io: Box<dyn Write> = if let Some(path) = &self.output_file {
            Box::new(BufWriter::new(File::create(path)?))
        } else {
            Box::new(BufWriter::new(std::io::stdout()))
        };

        Ok(io)
    }

    ///
    /// 入力ファイルの取得
    ///
    /// # 戻り値
    /// 変換結果の出力先のオープンに成功した場合は、FileオブジェクトをOk()でラ
    /// ップして返す。失敗した場合はstd::io::ErrorをErr()でラップして返す。
    ///
    pub fn get_input_file(&self) -> Result<File, Error> {
        Ok(File::open(&self.input_file)?)
    }

    ///
    /// 記録時刻の取得
    ///
    /// # 戻り値
    /// 入力ファイル名に記録開始時刻が埋め込まれている場合、その記録開始時刻と
    /// コマンドラインオプションで指定されたタイムゾーンに則って算出されたミリ
    /// 秒単位のUNIX時刻をSome()でラップして返す。そうでない場合はNoneを返す。
    /// コマンドラインオプションで指定されたタイムゾーン文字列が未知のもの（不
    /// 正なもの）の場合場合もNoneを返す。
    ///
    pub fn get_record_time(&self) -> Option<i64> {
        match (self.get_timeinfo(), self.get_timezone()) {
            (Some(info), Ok(tz)) => {
                match try_convert_to_unix_mills(info, tz) {
                    Ok(time) => Some(time),
                    Err(_) => {
                        eprintln!("Invalid datetime format in file name: {}",
                                  self.input_file);
                        None
                    } 
                }
            }

            _ => None
        }
    }
}

///
/// コマンドラインのパース
///
/// # 戻り値
/// コマンドライン引数をパースした結果をまとめたオブジェクト
///
pub fn parse() -> Options {
    Options::parse()
}
