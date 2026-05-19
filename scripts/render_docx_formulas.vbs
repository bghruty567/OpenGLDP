Option Explicit

Const ForReading = 1
Const TristateTrue = -1
Const wdFindStop = 0

Dim args, docPath, mapPath
Set args = WScript.Arguments

If args.Count < 2 Then
    WScript.Echo "Usage: render_docx_formulas.vbs <docx-path> <map-path>"
    WScript.Quit 64
End If

docPath = args(0)
mapPath = args(1)

Dim fso, formulaMap, mapFile, line, tabPos, token, latex
Set fso = CreateObject("Scripting.FileSystemObject")
Set formulaMap = CreateObject("Scripting.Dictionary")

If Not fso.FileExists(docPath) Then
    WScript.Echo "Document not found: " & docPath
    WScript.Quit 66
End If

If Not fso.FileExists(mapPath) Then
    WScript.Echo "Formula map not found: " & mapPath
    WScript.Quit 66
End If

Set mapFile = fso.OpenTextFile(mapPath, ForReading, False, TristateTrue)
Do Until mapFile.AtEndOfStream
    line = mapFile.ReadLine
    If Len(line) > 0 Then
        tabPos = InStr(line, vbTab)
        If tabPos > 0 Then
            token = Left(line, tabPos - 1)
            latex = Mid(line, tabPos + 1)
            formulaMap.Add token, latex
        End If
    End If
Loop
mapFile.Close

Dim word, doc, rng, key, failures
failures = 0

Set word = CreateObject("Word.Application")
word.Visible = False
word.DisplayAlerts = 0
Set doc = word.Documents.Open(docPath, False, False, False)

For Each key In formulaMap.Keys
    Set rng = doc.Content
    With rng.Find
        .ClearFormatting
        .Replacement.ClearFormatting
        .Text = key
        .Forward = True
        .Wrap = wdFindStop
        .Format = False
        .MatchCase = True
        .MatchWholeWord = False
        .MatchWildcards = False
    End With

    If rng.Find.Execute Then
        rng.Text = formulaMap.Item(key)
        doc.OMaths.Add rng
        If rng.OMaths.Count > 0 Then
            rng.OMaths(1).BuildUp
        ElseIf doc.OMaths.Count > 0 Then
            doc.OMaths(doc.OMaths.Count).BuildUp
        Else
            failures = failures + 1
        End If
    Else
        failures = failures + 1
    End If
Next

doc.Save
doc.Close False
word.Quit

If failures > 0 Then
    WScript.Echo "Formula replacements failed: " & failures
    WScript.Quit 2
End If

WScript.Echo docPath
