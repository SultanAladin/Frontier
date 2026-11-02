/*====================================================================================================================================
                                                             CATALOGUE.MJS
====================================================================================================================================*/
// 🧩 Store and library catalogue entries for the launcher prototype

export const Catalogue = Object.freeze([
    {
        Identifier: "control",
        Title: "Control",
        Studio: "Remedy Entertainment",
        Genre: "Action",
        Category: "action",
        Description: "A corruptive presence has invaded the Federal Bureau of Control. Only you have the power to stop it.",
        Cover: "assets/art/control.jpg",
        Wide: "assets/art/control-wide.jpg",
        Price: 39.99,
        Size: "42 GB",
        Megabytes: 43008,
        Owned: false,
        Installed: false,
        UpdateAvailable: false,
        Featured: true
    },
    {
        Identifier: "cyberpunk",
        Title: "Cyberpunk 2077",
        Studio: "CD Projekt Red",
        Genre: "Action RPG",
        Category: "action",
        Description: "Become an urban mercenary equipped with cybernetic enhancements and build your legend on the streets of Night City.",
        Cover: "assets/art/cyberpunk.jpg",
        Wide: "assets/art/cyberpunk.jpg",
        Price: 59.99,
        Size: "70 GB",
        Megabytes: 71680,
        Owned: true,
        Installed: true,
        UpdateAvailable: false,
        Featured: true
    },
    {
        Identifier: "death-stranding",
        Title: "Death Stranding",
        Studio: "Kojima Productions",
        Genre: "Adventure",
        Category: "adventure",
        Description: "Traverse a fractured world, reconnect isolated cities and carry the last threads of hope across a fallen landscape.",
        Cover: "assets/art/death-stranding.jpg",
        Wide: "assets/art/death-stranding.jpg",
        Price: 39.99,
        Size: "80 GB",
        Megabytes: 81920,
        Owned: true,
        Installed: true,
        UpdateAvailable: true,
        Featured: false
    },
    {
        Identifier: "elden-ring",
        Title: "Elden Ring",
        Studio: "FromSoftware",
        Genre: "Action RPG",
        Category: "action",
        Description: "Rise, Tarnished, and be guided by grace to brandish the power of the Elden Ring in the Lands Between.",
        Cover: "assets/art/elden-ring.jpg",
        Wide: "assets/art/elden-ring.jpg",
        Price: 59.99,
        Size: "60 GB",
        Megabytes: 61440,
        Owned: true,
        Installed: true,
        UpdateAvailable: false,
        Featured: false
    },
    {
        Identifier: "forza",
        Title: "Forza Horizon 5",
        Studio: "Playground Games",
        Genre: "Racing",
        Category: "racing",
        Description: "Explore the vibrant open-world landscapes of Mexico with limitless, fun driving action in hundreds of the world's greatest cars.",
        Cover: "assets/art/forza.jpg",
        Wide: "assets/art/forza-wide.jpg",
        Price: 59.99,
        Size: "110 GB",
        Megabytes: 112640,
        Owned: false,
        Installed: false,
        UpdateAvailable: false,
        Featured: true
    },
    {
        Identifier: "alan-wake",
        Title: "Alan Wake 2",
        Studio: "Remedy Entertainment",
        Genre: "Survival horror",
        Category: "adventure",
        Description: "A string of ritualistic murders threatens Bright Falls. Dive into a dark psychological horror story where two realities intertwine.",
        Cover: "assets/art/alan-wake.jpg",
        Wide: "assets/art/alan-wake.jpg",
        Price: 49.99,
        Size: "90 GB",
        Megabytes: 92160,
        Owned: true,
        Installed: false,
        UpdateAvailable: false,
        Featured: false
    }
]);

export function ResolveCatalogueEntry(Identifier)
{
    return Catalogue.find(Entry => Entry.Identifier === Identifier);
}

export function FormatPrice(Price)
{
    return Price === 0 ? "Free" : `$${Price.toFixed(2)}`;
}
