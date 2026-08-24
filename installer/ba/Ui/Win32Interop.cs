using System;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Data;
using Microsoft.Win32;

namespace Muffin.Setup.Ui
{
    /// <summary>True → Collapsed, False → Visible (pairs with BooleanToVisibilityConverter).</summary>
    public class InverseBoolToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            return value is bool b && b ? Visibility.Collapsed : Visibility.Visible;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            throw new NotSupportedException();
        }
    }

    /// <summary>
    /// Small DWM/registry helpers for the Windows 11 look: dark title bar on
    /// systems that support it, Mica backdrop where available, and the system
    /// light/dark app theme used to pick the palette.
    /// </summary>
    internal static class Win32Interop
    {
        private const int DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
        private const int DWMWA_SYSTEMBACKDROP_TYPE = 38;
        private const int DWMSBT_MAINWINDOW = 2;  // Mica

        [DllImport("dwmapi.dll")]
        private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute, ref int value, int size);

        public static void ApplyModernChrome(IntPtr hwnd, bool dark)
        {
            if (hwnd == IntPtr.Zero)
            {
                return;
            }

            var darkValue = dark ? 1 : 0;
            try
            {
                DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, ref darkValue, sizeof(int));

                // Mica on 22H2+; silently a no-op (nonzero hr) elsewhere and
                // the window keeps its solid palette background.
                var mica = DWMSBT_MAINWINDOW;
                DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, ref mica, sizeof(int));
            }
            catch (DllNotFoundException)
            {
                // Pre-Win10 DWM entry points: keep the classic look.
            }
        }

        public static bool SystemPrefersDarkTheme()
        {
            try
            {
                using (var key = Registry.CurrentUser.OpenSubKey(
                    @"Software\Microsoft\Windows\CurrentVersion\Themes\Personalize"))
                {
                    if (key?.GetValue("AppsUseLightTheme") is int light)
                    {
                        return light == 0;
                    }
                }
            }
            catch
            {
                // Fall through to the light theme.
            }
            return false;
        }
    }
}
