// Kleine hulpfuncties voor de game-adapters.
using System;
using System.Diagnostics;
using System.Reflection;
using HarmonyLib;
using UnityEngine;

namespace RavenGun.Unity
{
    public static class Util
    {
        public const BindingFlags AnyInstance = BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic;
        public const BindingFlags AnyStatic = BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;

        // Verbergt of toont een UI-element visueel (alle kinderen) via een CanvasGroup; de logica van de game merkt er niets van.
        public static void SetVisible(Transform t, bool visible)
        {
            if (t == null) return;
            CanvasGroup g = t.GetComponent<CanvasGroup>();
            if (g == null)
            {
                if (visible) return;   // niets te herstellen
                g = t.gameObject.AddComponent<CanvasGroup>();
                g.interactable = false;
                g.blocksRaycasts = false;
            }
            g.alpha = visible ? 1f : 0f;
        }

        // Diagnose (alleen bij Log/Uitgebreid): wie sluit de game af? Logt de aanroep met stack trace; wijzigt niets.
        public static void TraceQuit(Harmony h, params Type[] types)
        {
            string[] names = { "Quit", "QuitToWindows", "KillGame", "QuitGame", "OnApplicationQuit" };
            foreach (Type t in types)
                foreach (string n in names)
                {
                    try
                    {
                        MethodBase m = AccessTools.Method(t, n);
                        if (m == null) continue;
                        h.Patch(m, new HarmonyMethod(typeof(Util), "QuitSeen"), null);
                    }
                    catch (Exception e) { Core.Log.LogWarning("Quit-diagnose " + t.Name + "." + n + ": " + e.Message); }
                }
            try
            {
                MethodBase m = AccessTools.Method(typeof(Application), "Quit");
                if (m != null) h.Patch(m, new HarmonyMethod(typeof(Util), "QuitSeen"), null);
            }
            catch (Exception e) { Core.Log.LogWarning("Quit-diagnose Application.Quit: " + e.Message); }
        }

        static void QuitSeen(MethodBase __originalMethod)
        {
            Core.Log.LogWarning("AFSLUITEN gezien: " + __originalMethod.DeclaringType.Name + "." + __originalMethod.Name + "\n" + new StackTrace(1, false));
        }

        public static T GetField<T>(object o, string name, T def)
        {
            if (o == null) return def;
            FieldInfo f = o.GetType().GetField(name, AnyInstance);
            if (f == null) return def;
            object v = f.GetValue(o);
            return v is T ? (T)v : def;
        }

        // Omzetten van pixels naar het kijkvenster van een camera; zonder camera genormaliseerd op de schermgrootte.
        public static Vector3 Viewport(Camera cam, float x, float y)
        {
            Vector3 px = new Vector3(x, y, 0f);
            if (cam != null) return cam.ScreenToViewportPoint(px);
            return new Vector3(Screen.width > 0 ? x / Screen.width : 0f, Screen.height > 0 ? y / Screen.height : 0f, 0f);
        }
    }
}
