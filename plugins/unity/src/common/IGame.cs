// Wat de kern van de plugin van een game nodig heeft. Elke game (Tomb Raider, Rabbids Hollywood) heeft een eigen
// implementatie die rechtstreeks tegen de typen van de eigen Assembly-CSharp.dll is gecompileerd.
namespace RavenGun.Unity
{
    public interface IGame
    {
        Variant Variant { get; }
        string Name { get; }

        // Hoofdthread, elke frame dat de game zelf MouseControl zou draaien: richt en schiet voor alle spelers.
        void DriveGuns(InputState s);
        // Laat alle vastgehouden triggers los (de daemon valt weg of de invoer wordt onbetrouwbaar).
        void ReleaseGuns();

        // Toetsen naar gamegebeurtenissen (hetzelfde als wat het I/O-bord zou melden).
        void PressButton(int kaboomButton);   // waarde van de enum KaboomButton.Button
        void AddCoin(int player);   // speler 0..3

        // Toestand voor de uitvoer naar de daemon.
        void PollStatus(OutputState o);

        // Vizier visueel verbergen of tonen.
        void SetCrosshairHidden(bool hidden);
    }
}
