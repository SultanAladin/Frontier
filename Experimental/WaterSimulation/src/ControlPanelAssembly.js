/*====================================================================================================================================
                                                    CONTROL PANEL ASSEMBLY.JS
====================================================================================================================================*/
// 🧩 Builds the settings drawer from the control catalogue and reports every interaction once

import { ControlSections, ModelDescriptors, FormatControlValue } from './ScenarioSettings.js';

export class ControlPanelAssembly
{
    constructor(HostElement, Settings, OnChange, Actions)
    {
        this.Host = HostElement;
        this.Settings = Settings;
        this.OnChange = OnChange;
        this.Actions = Actions ?? [];
        this.Rows = new Map();

        this.Build();
    }

    Build()
    {
        this.Host.textContent = '';

        for (const Section of ControlSections)
        {
            this.BuildSection(Section);
        }

        if (this.Actions.length > 0)
        {
            const ActionSection = document.createElement('div');
            ActionSection.className = 'ControlSection';
            const Title = document.createElement('div');
            Title.className = 'SectionTitle';
            Title.textContent = 'Capture and checks';
            ActionSection.append(Title);

            for (const Action of this.Actions)
            {
                const Button = document.createElement('button');
                Button.className = 'ActionButton';
                Button.type = 'button';
                Button.textContent = Action.Label;
                Button.style.marginBottom = '8px';
                Button.addEventListener('click', () => Action.Run());
                ActionSection.append(Button);
            }

            this.Host.append(ActionSection);
        }
    }

    BuildSection(Section)
    {
        const SectionElement = document.createElement('div');
        SectionElement.className = 'ControlSection';

        const Title = document.createElement('div');
        Title.className = 'SectionTitle';
        Title.textContent = Section.Title;
        SectionElement.append(Title);

        for (const Control of Section.Controls)
        {
            if (Control.Category === 'model')
            {
                SectionElement.append(this.BuildModelChoice(Control));
                continue;
            }

            if (Control.Category === 'range')
            {
                SectionElement.append(this.BuildRange(Control));
                continue;
            }

            if (Control.Category === 'switch')
            {
                SectionElement.append(this.BuildSwitch(Control));
                continue;
            }

            if (Control.Category === 'segments')
            {
                SectionElement.append(this.BuildSegments(Control));
            }
        }

        this.Host.append(SectionElement);
    }

    BuildModelChoice(Control)
    {
        const Wrapper = document.createElement('div');
        Wrapper.className = 'ModelChoice';

        for (const Model of ModelDescriptors)
        {
            const Option = document.createElement('div');
            Option.className = 'ModelOption';
            Option.dataset.value = String(Model.Index - 1);

            const Index = document.createElement('div');
            Index.className = 'ModelIndex';
            Index.textContent = String(Model.Index);

            const Name = document.createElement('div');
            Name.className = 'ModelName';
            Name.textContent = Model.Label;

            const Note = document.createElement('div');
            Note.className = 'ModelNote';
            Note.textContent = Model.Note;

            Option.append(Index, Name, Note);
            Option.addEventListener('click', () => this.OnChange(Control.Key, Model.Index - 1));
            Wrapper.append(Option);
        }

        this.Rows.set(Control.Key, {
            Category: 'model',
            Update: (Value) =>
            {
                for (const Option of Wrapper.children)
                {
                    Option.classList.toggle('Selected', Number(Option.dataset.value) === Number(Value));
                }
            }
        });

        return Wrapper;
    }

    BuildRange(Control)
    {
        const Row = document.createElement('div');
        Row.className = 'ControlRow';

        const Label = document.createElement('div');
        Label.className = 'ControlLabel';

        const Name = document.createElement('span');
        Name.textContent = Control.Label;

        const ValueText = document.createElement('span');
        ValueText.className = 'ControlValue';

        Label.append(Name, ValueText);

        const Input = document.createElement('input');
        Input.type = 'range';
        Input.min = String(Control.Min);
        Input.max = String(Control.Max);
        Input.step = String(Control.Step);
        Input.value = String(this.Settings[Control.Key]);
        Input.addEventListener('input', () => this.OnChange(Control.Key, Number.parseFloat(Input.value)));

        Row.append(Label, Input);

        if (Control.Hint)
        {
            const Hint = document.createElement('div');
            Hint.className = 'ControlHint';
            Hint.textContent = Control.Hint;
            Row.append(Hint);
        }

        this.Rows.set(Control.Key, {
            Category: 'range',
            Update: (Value) =>
            {
                Input.value = String(Value);
                ValueText.textContent = FormatControlValue(Control, Value);
            }
        });

        return Row;
    }

    BuildSwitch(Control)
    {
        const Row = document.createElement('div');
        Row.className = 'ControlRow';

        const Toggle = document.createElement('div');
        Toggle.className = 'ToggleSwitch';

        const Name = document.createElement('span');
        Name.textContent = Control.Label;

        const Track = document.createElement('span');
        Track.className = 'ToggleTrack';

        Toggle.append(Name, Track);
        Toggle.addEventListener('click', () => this.OnChange(Control.Key, !this.Settings[Control.Key]));
        Row.append(Toggle);

        if (Control.Hint)
        {
            const Hint = document.createElement('div');
            Hint.className = 'ControlHint';
            Hint.textContent = Control.Hint;
            Row.append(Hint);
        }

        this.Rows.set(Control.Key, {
            Category: 'switch',
            Update: (Value) => Toggle.classList.toggle('On', !!Value)
        });

        return Row;
    }

    BuildSegments(Control)
    {
        const Row = document.createElement('div');
        Row.className = 'ControlRow';

        const Label = document.createElement('div');
        Label.className = 'ControlLabel';
        const Name = document.createElement('span');
        Name.textContent = Control.Label;
        Label.append(Name);

        const Segments = document.createElement('div');
        Segments.className = 'SegmentedRow';
        const Buttons = [];

        Control.OptionLabels.forEach((OptionLabel, OptionIndex) =>
        {
            const Button = document.createElement('div');
            Button.className = 'SegmentButton';
            Button.textContent = OptionLabel;
            Button.addEventListener('click', () => this.OnChange(Control.Key, OptionIndex));
            Segments.append(Button);
            Buttons.push(Button);
        });

        Row.append(Label, Segments);

        this.Rows.set(Control.Key, {
            Category: 'segments',
            Update: (Value) =>
            {
                Buttons.forEach((Button, ButtonIndex) => Button.classList.toggle('Selected', ButtonIndex === Number(Value)));
            }
        });

        return Row;
    }

    Synchronize()
    {
        for (const [Key, Row] of this.Rows)
        {
            Row.Update(this.Settings[Key]);
        }
    }
}
