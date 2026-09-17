using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;

// Launch Arduino CLI with a sketchbook scoped to this repository.
class ArduinoLocal
{
    static string Quote(string value)
    {
        return "\"" + Regex.Replace(value, "(\\\\*)\"", "$1$1\\\"")
            .TrimEnd('\\') + new string('\\', value.Length - value.TrimEnd('\\').Length) 
            + new string('\\', value.Length - value.TrimEnd('\\').Length) + "\"";
    }

    static int Main(string[] args)
    {
        try
        {
            string root = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "..", ".."));
            string extensions = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".vscode", "extensions");
            string cli = Directory.GetDirectories(extensions, "vscode-arduino.vscode-arduino-community-*")
                .OrderByDescending(Directory.GetLastWriteTimeUtc)
                .Select(dir => Path.Combine(dir, "assets", "platform", "win32-x64", "arduino-cli", "arduino-cli.exe"))
                .FirstOrDefault(File.Exists);
            if (cli == null) throw new Exception("Instala Arduino Community en VS Code.");
            var start = new ProcessStartInfo(cli, string.Join(" ", args.Select(Quote)));
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            start.RedirectStandardOutput = true;
            start.RedirectStandardError = true;
            start.WorkingDirectory = root;
            start.EnvironmentVariables["ARDUINO_DIRECTORIES_USER"] = root;
            using (var child = Process.Start(start))
            {
                child.OutputDataReceived += (sender, line) => { if (line.Data != null) Console.Out.WriteLine(line.Data); };
                child.ErrorDataReceived += (sender, line) => { if (line.Data != null) Console.Error.WriteLine(line.Data); };
                child.BeginOutputReadLine();
                child.BeginErrorReadLine();
                child.WaitForExit();
                return child.ExitCode;
            }
        }
        catch (Exception error) { Console.Error.WriteLine(error.Message); return 1; }
    }
}
