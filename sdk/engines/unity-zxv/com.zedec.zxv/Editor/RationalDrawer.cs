// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using UnityEditor;
using UnityEngine;

namespace Zedec.Zxv.Editor
{
    /// <summary>
    /// Shows a <see cref="Rational"/> as one text field ("3/4", "12.75",
    /// "-2"). The text is parsed exactly by libzxv and stored as the two
    /// integer fields num and den; text that is not an exact number (an
    /// exponent, "NaN", a stray space) is refused and the field turns red.
    /// There is no slider: a slider would produce floats.
    /// </summary>
    [CustomPropertyDrawer(typeof(Rational))]
    public sealed class RationalDrawer : PropertyDrawer
    {
        public override void OnGUI(Rect position, SerializedProperty property, GUIContent label)
        {
            SerializedProperty num = property.FindPropertyRelative("num");
            SerializedProperty den = property.FindPropertyRelative("den");
            long n = num.longValue, d = den.longValue == 0 && n == 0 ? 1 : den.longValue;
            string shown = n + "/" + d;

            EditorGUI.BeginProperty(position, label, property);
            string key = property.propertyPath + "#zxv-text";
            string edited = SessionState.GetString(key, shown);
            bool valid = Rational.TryParse(edited, out Rational parsed);
            Color old = GUI.color;
            if (!valid) GUI.color = new Color(1f, 0.55f, 0.55f); // editor colour only
            EditorGUI.BeginChangeCheck();
            edited = EditorGUI.DelayedTextField(position, label, edited);
            GUI.color = old;
            if (EditorGUI.EndChangeCheck())
            {
                if (Rational.TryParse(edited, out parsed))
                {
                    num.longValue = parsed.Numerator;
                    den.longValue = parsed.Denominator;
                    SessionState.EraseString(key);
                }
                else
                {
                    SessionState.SetString(key, edited); // keep the bad text visible, store nothing
                    Debug.LogWarning("Rational: '" + edited + "' is not an exact number (use 0.5, 1/2 or 2)");
                }
            }
            else if (valid && parsed.Numerator == n && parsed.Denominator == d)
            {
                SessionState.EraseString(key);
            }
            EditorGUI.EndProperty();
        }
    }

    /// <summary>Shows a <see cref="Hexagram"/> as its name, with its tag.</summary>
    [CustomPropertyDrawer(typeof(Hexagram))]
    public sealed class HexagramDrawer : PropertyDrawer
    {
        public override void OnGUI(Rect position, SerializedProperty property, GUIContent label)
        {
            SerializedProperty tag = property.FindPropertyRelative("tag");
            EditorGUI.BeginProperty(position, label, property);
            int value = Mathf.Clamp(tag.intValue, 0, 63);
            string name = Hexagram.FromTag(value).Name;
            int edited = EditorGUI.IntSlider(position, new GUIContent(label.text + " (" + name + ")"), value, 0, 63);
            if (edited != value) tag.intValue = edited;
            EditorGUI.EndProperty();
        }
    }
}
